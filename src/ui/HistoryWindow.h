#pragma once

#include <QHash>
#include <QPixmap>
#include <QVector>
#include <QWidget>

#include "core/Entry.h"

class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
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

    Store &m_store;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_summary = nullptr;
    QHash<QString, QPixmap> m_thumbCache;
    QVector<Entry> m_entries;
    qint64 m_expandedId = -1;
};
