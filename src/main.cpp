#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

#include <memory>

#include "core/Store.h"
#include "platform/ClipboardSource.h"
#include "platform/ClipboardWatcher.h"
#include "platform/XcbClipboardWatcher.h"
#include "ui/HistoryWindow.h"
#include "ui/TrayController.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("clipman"));
    QApplication::setApplicationName(QStringLiteral("clipman"));
    QApplication::setApplicationDisplayName(QStringLiteral("Clipman"));
    QApplication::setQuitOnLastWindowClosed(false);

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDir.isEmpty() || !QDir().mkpath(dataDir)) {
        qCritical() << "无法创建数据目录:" << dataDir;
        return 1;
    }

    QLockFile lock(dataDir + QStringLiteral("/clipman.lock"));
    if (!lock.tryLock(0)) {
        qWarning() << "Clipman 已在运行";
        return 0;
    }

    Store store;
    QString error;
    if (!store.open(dataDir + QStringLiteral("/history.db"), &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("Clipman"),
                              QStringLiteral("打开历史数据库失败：%1").arg(error));
        return 1;
    }

    HistoryWindow window(store);

    std::unique_ptr<ClipboardSource> source = XcbClipboardWatcher::create();
    if (!source) {
        qWarning() << "XFixes 不可用，回退到 QClipboard 监听";
        source = std::make_unique<ClipboardWatcher>(QGuiApplication::clipboard());
    }

    TrayController tray(&window);

    QObject::connect(source.get(), &ClipboardSource::textCaptured, &window,
                     [&](const QString &text) {
                         if (!store.addText(text, &error))
                             qWarning() << "写入历史失败:" << error;
                         if (window.isVisible())
                             window.refresh();
                     });

    QObject::connect(&window, &HistoryWindow::entryActivated, &window,
                     [&](const QString &text) {
                         source->suppressNext(text);
                         QGuiApplication::clipboard()->setText(text);
                     });

    QObject::connect(&tray, &TrayController::clearRequested, &window, [&] {
        store.clearAll();
        window.refresh();
    });
    QObject::connect(&tray, &TrayController::quitRequested, &app, &QApplication::quit);

    source->start();

    if (tray.isAvailable()) {
        tray.show();
    } else {
        qWarning() << "系统托盘不可用，改为直接显示主窗口";
        window.show();
    }

    return app.exec();
}
