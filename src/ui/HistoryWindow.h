#pragma once

#include <QHash>
#include <QPixmap>
#include <QWidget>

class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class Store;
struct Entry;

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
    void cleared();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void activateCurrent();
    void deleteCurrent();
    QString entryLabel(const Entry &entry) const;
    QPixmap thumbnail(const Entry &entry);

    Store &m_store;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_summary = nullptr;
    QHash<QString, QPixmap> m_thumbCache;
};
