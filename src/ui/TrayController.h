#pragma once

#include <QObject>

class HistoryWindow;
class QMenu;
class QSystemTrayIcon;
class QTimer;

class TrayController : public QObject
{
    Q_OBJECT

public:
    explicit TrayController(HistoryWindow *window, QObject *parent = nullptr);

    bool isAvailable() const;
    // 托盘宿主未就绪时自动重试，全部失败则放弃（不影响快捷键唤起）。
    void show();

signals:
    void clearRequested();
    void quitRequested();
    void pauseToggled(bool paused);
    void settingsRequested();

private:
    void retryShow();

    HistoryWindow *m_window = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QTimer *m_retryTimer = nullptr;
    int m_retriesLeft = 0;
};
