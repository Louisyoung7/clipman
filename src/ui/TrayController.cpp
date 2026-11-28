#include "ui/TrayController.h"

#include <QColor>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSystemTrayIcon>

#include "ui/HistoryWindow.h"

namespace {

QIcon fallbackIcon()
{
    QPixmap pixmap(32, 32);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);

    const QColor accent(0x35, 0x84, 0xe4);
    painter.setBrush(accent);
    painter.drawRoundedRect(QRectF(4, 7, 24, 22), 4, 4);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(QRectF(10, 3, 12, 7), 2, 2);
    painter.setBrush(accent);
    painter.drawRoundedRect(QRectF(12, 5, 8, 3), 1, 1);
    painter.end();

    return QIcon(pixmap);
}

} // namespace

TrayController::TrayController(HistoryWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    m_tray = new QSystemTrayIcon(this);

    QIcon icon = QIcon::fromTheme(QStringLiteral("clipory"));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("edit-paste"));
    if (icon.isNull())
        icon = QIcon::fromTheme(QStringLiteral("edit-copy"));
    if (icon.isNull())
        icon = fallbackIcon();

    m_tray->setIcon(icon);
    m_tray->setToolTip(tr("Clipory 剪贴板历史"));

    m_menu = new QMenu;
    QAction *showAction = m_menu->addAction(tr("显示历史"));
    QAction *pauseAction = m_menu->addAction(tr("暂停记录"));
    pauseAction->setCheckable(true);
    QAction *clearAction = m_menu->addAction(tr("清空历史"));
    QAction *settingsAction = m_menu->addAction(tr("设置…"));
    m_menu->addSeparator();
    QAction *quitAction = m_menu->addAction(tr("退出"));
    m_tray->setContextMenu(m_menu);

    connect(showAction, &QAction::triggered, this, [this] { m_window->showPanel(); });
    connect(pauseAction, &QAction::toggled, this, [this](bool paused) {
        m_tray->setToolTip(paused ? tr("Clipory 已暂停记录") : tr("Clipory 剪贴板历史"));
        emit pauseToggled(paused);
    });
    connect(clearAction, &QAction::triggered, this, &TrayController::clearRequested);
    connect(settingsAction, &QAction::triggered, this, &TrayController::settingsRequested);
    connect(quitAction, &QAction::triggered, this, &TrayController::quitRequested);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick)
                    return;
                m_window->togglePanel();
            });
}

bool TrayController::isAvailable() const
{
    return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayController::show()
{
    m_tray->show();
}
