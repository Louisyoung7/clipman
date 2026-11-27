#include "ui/HistoryWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLoggingCategory>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include "core/Store.h"
#include "ui/FavoriteItemDelegate.h"
#include "ui/FolderEditPopup.h"

Q_LOGGING_CATEGORY(lcHistoryWindow, "clipman.ui.historywindow", QtWarningMsg)

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

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索剪贴板历史…"));
    m_search->setClearButtonEnabled(true);

    auto *topRow = new QHBoxLayout;
    topRow->addWidget(m_search);

    m_folderCombo = new QComboBox(this);
    m_folderCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_folderCombo->setToolTip(tr("筛选收藏夹"));
    topRow->addWidget(m_folderCombo);

    m_manageButton = new QToolButton(this);
    m_manageButton->setText(QStringLiteral("⋯"));
    m_manageButton->setToolTip(tr("管理收藏夹"));
    topRow->addWidget(m_manageButton);

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

    m_starDelegate = new FavoriteItemDelegate(this);
    m_list->setItemDelegate(m_starDelegate);

    m_summary = new QLabel(this);

    auto *footer = new QHBoxLayout;
    footer->addWidget(m_summary);
    footer->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_list);
    layout->addLayout(footer);

    connect(m_search, &QLineEdit::textChanged, this, &HistoryWindow::refresh);
    connect(m_folderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                m_currentFolderId = m_folderCombo->itemData(index).toLongLong();
                refresh();
            });
    connect(m_manageButton, &QToolButton::clicked, this, &HistoryWindow::manageFolders);
    connect(m_starDelegate, &FavoriteItemDelegate::starToggled, this,
            &HistoryWindow::toggleStar);
    connect(m_starDelegate, &FavoriteItemDelegate::starLongPressed, this,
            &HistoryWindow::showStarMenu);

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

void HistoryWindow::refresh()
{
    reloadFolders();
    const QString query = m_search ? m_search->text() : QString();
    m_favoriteMap = m_store.favoriteMap();
    m_entries = m_store.search(query, m_currentFolderId);
    m_expandedId = -1;

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

    if (m_list->count() > 0)
        m_list->setCurrentRow(0);

    m_summary->setText(
        tr("共 %1 条 · 星星短按收藏 / 长按选收藏夹 · 双击或 Enter 复制 · Delete 删除 · Esc 关闭")
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
    qCDebug(lcHistoryWindow) << "hide, activePopup" << QApplication::activePopupWidget()
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

void HistoryWindow::reloadFolders()
{
    const QSignalBlocker blocker(m_folderCombo);
    m_folderCombo->clear();
    m_folderCombo->addItem(tr("全部历史"), -1);
    for (const Folder &folder : m_store.folders())
        m_folderCombo->addItem(folder.name, folder.id);

    const int index = m_folderCombo->findData(m_currentFolderId);
    if (index < 0)
        m_currentFolderId = -1;
    m_folderCombo->setCurrentIndex(index >= 0 ? index : 0);
}

void HistoryWindow::toggleStar(qint64 entryId)
{
    // 已收藏（任意收藏夹）→ 全部取消；未收藏 → 加入默认收藏夹。
    if (m_favoriteMap.contains(entryId)) {
        for (qint64 folderId : m_favoriteMap.value(entryId))
            m_store.setMembership(entryId, folderId, false);
    } else {
        m_store.setMembership(entryId, Store::kDefaultFolderId, true);
    }
    refresh();
}

void HistoryWindow::showStarMenu(qint64 entryId, const QPoint &globalPos)
{
    qCDebug(lcHistoryWindow) << "star long pressed" << entryId << globalPos
                             << "window visible" << isVisible();
    QMenu menu(this);
    const QVector<qint64> current = m_favoriteMap.value(entryId);
    for (const Folder &folder : m_store.folders()) {
        QAction *action = menu.addAction(folder.name);
        action->setCheckable(true);
        action->setChecked(current.contains(folder.id));
        action->setData(folder.id);
    }
    menu.addSeparator();
    QAction *addAction = menu.addAction(tr("新建收藏夹…"));
    QAction *manageAction = menu.addAction(tr("管理收藏夹…"));

    QAction *chosen = menu.exec(globalPos);
    qCDebug(lcHistoryWindow) << "star menu chosen"
                             << (chosen ? chosen->text() : QStringLiteral("none"));
    if (!chosen)
        return;

    if (chosen == addAction) {
        qint64 folderId = 0;
        if (createFolder(QCursor::pos(), &folderId))
            m_store.setMembership(entryId, folderId, true);
    } else if (chosen == manageAction) {
        manageFolders();
    } else {
        m_store.setMembership(entryId, chosen->data().toLongLong(), !chosen->isChecked());
    }
    refresh();
}

bool HistoryWindow::createFolder(const QPoint &anchor, qint64 *folderId)
{
    const bool wasVisible = isVisible();
    bool ok = false;
    const QString name = FolderEditPopup::getText(tr("新建收藏夹"), QString(), anchor, &ok);
    qCDebug(lcHistoryWindow) << "folder popup closed" << ok << name
                             << "window visible" << isVisible();
    // Tool 窗口激活不会自动隐藏 Popup 面板；若仍被隐藏则恢复，避免用户回到空白界面。
    if (wasVisible && !isVisible())
        show();
    if (!ok || name.isEmpty())
        return false;

    QString error;
    if (!m_store.addFolder(name, folderId, &error)) {
        QMessageBox::warning(this, tr("新建收藏夹失败"), error);
        return false;
    }
    return true;
}

void HistoryWindow::manageFolders()
{
    QMenu menu(this);
    for (const Folder &folder : m_store.folders()) {
        if (folder.isDefault)
            continue;

        QMenu *sub = menu.addMenu(folder.name);
        QAction *rename = sub->addAction(tr("重命名…"));
        QAction *remove = sub->addAction(tr("删除"));
        connect(rename, &QAction::triggered, this, [this, folder] {
            bool ok = false;
            const QString name = FolderEditPopup::getText(tr("重命名收藏夹"), folder.name,
                                                          QCursor::pos(), &ok);
            if (!ok || name.isEmpty())
                return;
            QString error;
            if (!m_store.renameFolder(folder.id, name, &error))
                QMessageBox::warning(this, tr("重命名失败"), error);
            refresh();
        });
        connect(remove, &QAction::triggered, this, [this, folder] {
            if (QMessageBox::question(this, tr("删除收藏夹"),
                                      tr("确定删除「%1」？其中的历史项不会被删除。")
                                          .arg(folder.name),
                                      QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                return;
            QString error;
            if (!m_store.removeFolder(folder.id, &error))
                QMessageBox::warning(this, tr("删除收藏夹失败"), error);
            if (m_currentFolderId == folder.id) {
                m_currentFolderId = -1;
                m_folderCombo->setCurrentIndex(0);
            }
            refresh();
        });
    }
    if (menu.actions().isEmpty())
        menu.addAction(tr("暂无自定义收藏夹"))->setEnabled(false);
    menu.addSeparator();
    QAction *addAction = menu.addAction(tr("新建收藏夹…"));
    connect(addAction, &QAction::triggered, this, [this] {
        if (createFolder(QCursor::pos()))
            refresh();
    });
    menu.exec(QCursor::pos());
}
