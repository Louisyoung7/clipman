#include "ui/HistoryWindow.h"

#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

#include "core/Store.h"

HistoryWindow::HistoryWindow(Store &store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setWindowTitle(tr("剪贴板历史"));
    setWindowFlags(Qt::Popup);
    resize(520, 480);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索剪贴板历史…"));
    m_search->setClearButtonEnabled(true);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);

    m_summary = new QLabel(this);
    auto *clearButton = new QPushButton(tr("清空"), this);

    auto *footer = new QHBoxLayout;
    footer->addWidget(m_summary);
    footer->addStretch();
    footer->addWidget(clearButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_search);
    layout->addWidget(m_list);
    layout->addLayout(footer);

    connect(m_search, &QLineEdit::textChanged, this, &HistoryWindow::refresh);

    connect(m_list, &QListWidget::itemClicked, this, &HistoryWindow::activateCurrent);
    connect(m_list, &QListWidget::itemActivated, this, &HistoryWindow::activateCurrent);
    connect(m_list, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_list->itemAt(pos);
        if (!item)
            return;
        m_list->setCurrentItem(item);

        QMenu menu(this);
        QAction *copyAction = menu.addAction(tr("复制"));
        QAction *deleteAction = menu.addAction(tr("删除"));
        QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
        if (chosen == copyAction)
            activateCurrent();
        else if (chosen == deleteAction)
            deleteCurrent();
    });

    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_store.clearAll();
        refresh();
        emit cleared();
    });

    m_search->installEventFilter(this);
    m_list->installEventFilter(this);

    refresh();
}

void HistoryWindow::refresh()
{
    const QString query = m_search ? m_search->text() : QString();
    const QVector<Entry> entries = m_store.search(query);

    m_list->clear();
    for (const Entry &entry : entries) {
        const QString stamp =
            QDateTime::fromMSecsSinceEpoch(entry.createdAt).toString(QStringLiteral("MM-dd HH:mm:ss"));
        auto *item = new QListWidgetItem(tr("%1  %2").arg(stamp, entry.text.simplified()));
        item->setData(Qt::UserRole, entry.text);
        item->setData(Qt::UserRole + 1, entry.id);
        item->setToolTip(entry.text.left(5000));
        m_list->addItem(item);
    }

    if (m_list->count() > 0)
        m_list->setCurrentRow(0);

    m_summary->setText(tr("共 %1 条 · ↑↓ 选择 · Enter 复制 · Delete 删除 · Esc 关闭")
                           .arg(entries.size()));
}

void HistoryWindow::showPanel()
{
    m_search->clear();
    refresh();

    const QPoint cursor = QCursor::pos();
    const QScreen *screen = QGuiApplication::screenAt(cursor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        const int maxX = qMax(available.left(), available.right() - width() + 1);
        const int maxY = qMax(available.top(), available.bottom() - height() + 1);
        const QPoint pos(qBound(available.left(), cursor.x() - width() / 2, maxX),
                         qBound(available.top(), cursor.y() - height() - 12, maxY));
        move(pos);
    }

    show();
    raise();
    activateWindow();
    m_search->setFocus();
}

void HistoryWindow::togglePanel()
{
    if (isVisible())
        hide();
    else
        showPanel();
}

bool HistoryWindow::eventFilter(QObject *watched, QEvent *event)
{
    if ((watched != m_search && watched != m_list) || event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    switch (keyEvent->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (m_list->count() == 0)
            return true;
        int row = m_list->currentRow();
        if (row < 0)
            row = 0;
        row += (keyEvent->key() == Qt::Key_Up) ? -1 : 1;
        m_list->setCurrentRow(qBound(0, row, m_list->count() - 1));
        return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
        activateCurrent();
        return true;
    case Qt::Key_Delete:
        deleteCurrent();
        return true;
    case Qt::Key_Escape:
        hide();
        return true;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

void HistoryWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void HistoryWindow::activateCurrent()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;

    emit entryActivated(item->data(Qt::UserRole).toString());
    hide();
}

void HistoryWindow::deleteCurrent()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;

    const qint64 id = item->data(Qt::UserRole + 1).toLongLong();
    m_store.removeEntry(id);
    refresh();
}
