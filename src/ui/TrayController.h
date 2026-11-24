#pragma once

#include <QObject>

class HistoryWindow;
class QAction;
class QMenu;
class QSystemTrayIcon;

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(HistoryWindow *window, QObject *parent = nullptr);

    bool isAvailable() const;
    void show();
    void setAutostartEnabled(bool enabled);

signals:
    void clearRequested();
    void quitRequested();
    void pauseToggled(bool paused);
    void autostartToggled(bool enabled);
    void settingsRequested();

private:
    HistoryWindow *m_window = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_autostartAction = nullptr;
};
