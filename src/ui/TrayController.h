#pragma once

#include <QObject>

class HistoryWindow;
class QMenu;
class QSystemTrayIcon;

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(HistoryWindow *window, QObject *parent = nullptr);

    bool isAvailable() const;
    void show();

signals:
    void clearRequested();
    void quitRequested();
    void pauseToggled(bool paused);

private:
    HistoryWindow *m_window = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
};
