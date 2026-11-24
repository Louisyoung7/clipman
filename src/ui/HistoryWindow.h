#pragma once

#include <QWidget>

class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QListWidget;
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
    void entryActivated(const QString &text);
    void cleared();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void activateCurrent();
    void deleteCurrent();

    Store &m_store;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_summary = nullptr;
};
