#pragma once

#include <QHash>
#include <QPixmap>
#include <QPoint>
#include <QVector>
#include <QWidget>

#include "core/Entry.h"

class FavoriteItemDelegate;
class QButtonGroup;
class QEvent;
class QHideEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QListWidget;
class QListWidgetItem;
class QShowEvent;
class Store;

class HistoryWindow : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryWindow(Store &store, QWidget *parent = nullptr);

    void refresh(bool preserveSelection = true);
    void showPanel();
    void hidePanel();
    void togglePanel();

signals:
    void entryActivated(qint64 id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    // 面板被显示后失去激活即隐藏（代替 Qt::Popup 的“点外部关闭”）。
    void maybeAutoHide();
    void activateCurrent();
    void deleteCurrent();
    void toggleTextExpansion(QListWidgetItem *item);
    const Entry *entryById(qint64 id) const;
    QString entryLabel(const Entry &entry, bool expanded) const;
    QPixmap thumbnail(const Entry &entry);
    void toggleStar(qint64 entryId);

    Store &m_store;
    QPushButton *m_historyTab = nullptr;
    QPushButton *m_favoriteTab = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_summary = nullptr;
    FavoriteItemDelegate *m_starDelegate = nullptr;
    QHash<qint64, QVector<qint64>> m_favoriteMap;
    QHash<QString, QPixmap> m_thumbCache;
    QVector<Entry> m_entries;
    bool m_favoritesOnly = false;
    qint64 m_lastStarClickAt = 0;
    qint64 m_expandedId = -1;
    // 面板可见性只以这里为准：show()/hide() 未必可靠（窗口可能被外部销毁或复位）。
    bool m_panelShown = false;
    qint64 m_shownAt = 0;
    bool m_autoHideSuppressed = false;
};
