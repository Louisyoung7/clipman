#pragma once

#include <QWidget>

class QLabel;
class QListWidget;
class Store;

class HistoryWindow : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryWindow(Store &store, QWidget *parent = nullptr);

    void refresh();

signals:
    void entryActivated(const QString &text);
    void cleared();

private:
    Store &m_store;
    QListWidget *m_list = nullptr;
    QLabel *m_summary = nullptr;
};
