#include "ui/HistoryWindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

#include "core/Store.h"
#include "ui/FavoriteItemDelegate.h"

Q_LOGGING_CATEGORY(lcHistory, "clipory.ui.history", QtWarningMsg)

namespace {
constexpr int kCollapsedTextChars = 120;
constexpr int kExpandedTextChars = 2000;
}

HistoryWindow::HistoryWindow(Store &store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setWindowTitle(tr("剪贴板历史"));
    setWindowFlags(Qt::Popup);
    resize(520, 480);

    const QString tabStyle = QStringLiteral(
        "QPushButton { padding: 4px 10px; border: 1px solid #d3d8e0; border-radius: 5px; "
        "background: #f7f8fa; color: #202124; }"
        "QPushButton:checked { background: #e8f0fe; border-color: #9dc0f7; color: #1a73e8; }");

    m_historyTab = new QPushButton(tr("历史"), this);
    m_historyTab->setCheckable(true);
    m_historyTab->setChecked(true);
    m_historyTab->setStyleSheet(tabStyle);

    m_favoriteTab = new QPushButton(tr("收藏"), this);
    m_favoriteTab->setCheckable(true);
    m_favoriteTab->setStyleSheet(tabStyle);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索剪贴板历史…"));
    m_search->setClearButtonEnabled(true);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_historyTab);
    topRow->addWidget(m_favoriteTab);
    topRow->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(false);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setWordWrap(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->setIconSize(QSize(64, 64));
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  border: none;"
        "  background: transparent;"
        "  outline: none;"
        "}"
        "QListWidget::item {"
        "  background: #f7f8fa;"
        "  border: 1px solid #e4e7ec;"
        "  border-radius: 7px;"
        "  padding: 8px 10px;"
        "  margin: 5px 3px;"
        "  color: #202124;"
        "}"
        "QListWidget::item:hover {"
        "  background: #eef1f6;"
        "  border-color: #d3d8e0;"
        "}"
        "QListWidget::item:selected {"
        "  background: #e8f0fe;"
        "  border-color: #9dc0f7;"
        "  color: #1a1a1a;"
        "}"));

    m_starDelegate = new FavoriteItemDelegate(m_list, this);
    m_list->setItemDelegate(m_starDelegate);

    m_summary = new QLabel(this);

    auto *footer = new QHBoxLayout;
    footer->addWidget(m_summary);
    footer->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_list);
    layout->addLayout(footer);

    auto *viewGroup = new QButtonGroup(this);
    viewGroup->setExclusive(true);
    viewGroup->addButton(m_historyTab, 0);
    viewGroup->addButton(m_favoriteTab, 1);
    connect(m_historyTab, &QPushButton::clicked, this, [this] {
        m_favoritesOnly = false;
        m_search->clear();
        refresh();
    });
    connect(m_favoriteTab, &QPushButton::clicked, this, [this] {
        m_favoritesOnly = true;
        m_search->clear();
        refresh();
    });

    connect(m_search, &QLineEdit::textChanged, this, [this] { refresh(false); });
    connect(m_starDelegate, &FavoriteItemDelegate::starToggled, this,
            &HistoryWindow::toggleStar);

    connect(m_list, &QListWidget::itemClicked, this, &HistoryWindow::toggleTextExpansion);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &HistoryWindow::activateCurrent);
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

    m_search->installEventFilter(this);
    m_list->installEventFilter(this);

    refresh();
}

QString HistoryWindow::entryLabel(const Entry &entry, bool expanded) const
{
    const QString stamp =
        QDateTime::fromMSecsSinceEpoch(entry.createdAt).toString(QStringLiteral("MM-dd HH:mm:ss"));

    switch (entry.kind) {
    case ClipKind::Image:
        return tr("%1  [图片] %2×%3").arg(stamp).arg(entry.width).arg(entry.height);
    case ClipKind::Files: {
        const QStringList paths = entry.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        return tr("%1  [文件] %2").arg(stamp, paths.join(QStringLiteral(" · ")));
    }
    case ClipKind::Text:
        break;
    }

    QString text = entry.text.simplified();
    const int limit = expanded ? kExpandedTextChars : kCollapsedTextChars;
    if (text.size() > limit)
        text = text.left(limit) + QStringLiteral("…");
    return tr("%1  %2").arg(stamp, text);
}

const Entry *HistoryWindow::entryById(qint64 id) const
{
    for (const Entry &entry : m_entries) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

void HistoryWindow::toggleTextExpansion(QListWidgetItem *item)
{
    if (!item)
        return;

    const qint64 id = item->data(Qt::UserRole + 1).toLongLong();
    const Entry *entry = entryById(id);
    if (!entry || entry->kind != ClipKind::Text)
        return;
    if (entry->text.simplified().size() <= kCollapsedTextChars)
        return;

    const bool expanded = m_expandedId != id;
    m_expandedId = expanded ? id : -1;
    item->setText(entryLabel(*entry, expanded));
    m_list->doItemsLayout();
    m_list->scrollToItem(item);
}

QPixmap HistoryWindow::thumbnail(const Entry &entry)
{
    const auto cached = m_thumbCache.constFind(entry.hash);
    if (cached != m_thumbCache.constEnd())
        return cached.value();

    QPixmap pixmap(m_store.blobStore().thumbnailPath(entry.hash));
    m_thumbCache.insert(entry.hash, pixmap);
    return pixmap;
}

void HistoryWindow::refresh(bool preserveSelection)
{
    const QString query = m_search ? m_search->text() : QString();
    m_favoriteMap = m_store.favoriteMap();
    m_entries = m_store.search(query, m_favoritesOnly ? Store::kDefaultFolderId : -1);
    m_expandedId = -1;

    // 收藏切换等局部更新后刷新，保留当前选中项和滚动位置，避免列表跳回顶部。
    const qint64 selectedId = preserveSelection && m_list && m_list->currentItem()
                                  ? m_list->currentItem()->data(Qt::UserRole + 1).toLongLong()
                                  : -1;
    const int scrollTop = preserveSelection && m_list ? m_list->verticalScrollBar()->value() : 0;

    m_list->clear();
    for (const Entry &entry : m_entries) {
        auto *item = new QListWidgetItem(entryLabel(entry, false));
        item->setData(Qt::UserRole + 1, entry.id);
        item->setData(Qt::UserRole + 2, m_favoriteMap.contains(entry.id));

        if (entry.kind == ClipKind::Image) {
            const QPixmap pixmap = thumbnail(entry);
            if (!pixmap.isNull())
                item->setIcon(QIcon(pixmap));
        }

        m_list->addItem(item);
    }

    int restoredRow = -1;
    if (selectedId >= 0) {
        for (int row = 0; row < m_list->count(); ++row) {
            if (m_list->item(row)->data(Qt::UserRole + 1).toLongLong() == selectedId) {
                restoredRow = row;
                break;
            }
        }
    }
    if (restoredRow >= 0)
        m_list->setCurrentRow(restoredRow);
    else if (m_list->count() > 0)
        m_list->setCurrentRow(0);
    m_list->verticalScrollBar()->setValue(scrollTop);

    m_summary->setText(
        tr("共 %1 条 · 点击星星收藏 · 双击或 Enter 复制 · Delete 删除 · Esc 关闭")
            .arg(m_entries.size()));
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

void HistoryWindow::hideEvent(QHideEvent *event)
{
    qCDebug(lcHistory) << "window hide, activePopup" << QApplication::activePopupWidget()
                       << "activeModal" << QApplication::activeModalWidget()
                       << "focusWindow" << QGuiApplication::focusWindow();
    QWidget::hideEvent(event);
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
    // 星星点击后紧跟着的双击信号不该触发复制；用时间窗覆盖事件顺序的不确定性。
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_lastStarClickAt;
    qCDebug(lcHistory) << "activateCurrent, sinceStarClick" << elapsed;
    if (m_lastStarClickAt > 0 && elapsed >= 0 && elapsed < 600)
        return;

    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;

    emit entryActivated(item->data(Qt::UserRole + 1).toLongLong());
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

void HistoryWindow::toggleStar(qint64 entryId)
{
    m_lastStarClickAt = QDateTime::currentMSecsSinceEpoch();

    const bool wasFavorite = m_favoriteMap.contains(entryId);
    qCDebug(lcHistory) << "toggleStar" << entryId << "wasFavorite" << wasFavorite
                       << "favoritesOnly" << m_favoritesOnly;
    m_store.setMembership(entryId, Store::kDefaultFolderId, !wasFavorite);

    // 以数据库为准重建列表，保证星星状态与 m_favoriteMap 始终一致；
    // 定位已改用 visualRect，重建不会再造成点击位置漂移。
    refresh();
}

