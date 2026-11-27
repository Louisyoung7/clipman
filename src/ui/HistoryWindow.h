#pragma once

#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QVector>
#include <QWidget>

#include "core/Entry.h"

class FavoriteItemDelegate;
class QComboBox;
class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QToolButton;
class Store;

class HistoryWindow : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryWindow(Store &store, QWidget *parent = nullptr);

    void refresh();
    void showPanel();
    void togglePanel();

signals:
    void entryActivated(qint64 id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void activateCurrent();
    void deleteCurrent();
    void toggleTextExpansion(QListWidgetItem *item);
    const Entry *entryById(qint64 id) const;
    QString entryLabel(const Entry &entry, bool expanded) const;
    QPixmap thumbnail(const Entry &entry);
    void reloadFolders();
    void toggleStar(qint64 entryId);
    void showStarMenu(qint64 entryId, const QPoint &globalPos);
    void manageFolders();
    bool createFolder(const QPoint &anchor, qint64 *folderId = nullptr);

    Store &m_store;
    QLineEdit *m_search = nullptr;
    QComboBox *m_folderCombo = nullptr;
    QToolButton *m_manageButton = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_summary = nullptr;
    FavoriteItemDelegate *m_starDelegate = nullptr;
    QHash<qint64, QVector<qint64>> m_favoriteMap;
    QHash<QString, QPixmap> m_thumbCache;
    QVector<Entry> m_entries;
    qint64 m_currentFolderId = -1;
    qint64 m_expandedId = -1;
};
