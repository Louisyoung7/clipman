#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QMessageBox>
#include <QMimeData>
#include <QStandardPaths>
#include <QUrl>

#include <memory>

#include "core/CaptureService.h"
#include "core/Store.h"
#include "platform/ClipboardSource.h"
#include "platform/ClipboardWatcher.h"
#include "platform/SingleInstance.h"
#include "platform/X11Hotkey.h"
#include "platform/XcbClipboardWatcher.h"
#include "ui/HistoryWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/TrayController.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("clipman"));
    QApplication::setApplicationName(QStringLiteral("clipman"));
    QApplication::setApplicationVersion(QStringLiteral(CLIPMAN_VERSION));
    QApplication::setApplicationDisplayName(QStringLiteral("Clipman"));
    QApplication::setDesktopFileName(QStringLiteral("clipman"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/clipman.svg")));
    QApplication::setQuitOnLastWindowClosed(false);

    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dataDir.isEmpty() || !QDir().mkpath(dataDir)) {
        qCritical() << "无法创建数据目录:" << dataDir;
        return 1;
    }

    SingleInstance single(dataDir);
    if (!single.tryBecomePrimary()) {
        single.notifyExisting();
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
    CaptureService capture(store.blobStore());

    std::unique_ptr<ClipboardSource> source = XcbClipboardWatcher::create();
    if (!source) {
        qWarning() << "XFixes 不可用，回退到 QClipboard 监听";
        source = std::make_unique<ClipboardWatcher>(QGuiApplication::clipboard());
    }

    TrayController tray(&window);

    std::unique_ptr<X11Hotkey> hotkey = X11Hotkey::create();
    if (hotkey) {
        QObject::connect(hotkey.get(), &X11Hotkey::activated, &window, &HistoryWindow::togglePanel);
    } else {
        qWarning() << "全局快捷键不可用（仅 X11 支持），请用托盘打开";
    }

    bool paused = false;

    QObject::connect(source.get(), &ClipboardSource::itemCaptured, &window,
                     [&](const ClipboardItem &item) {
                         if (paused)
                             return;
                         capture.submit(item);
                     });

    QObject::connect(&capture, &CaptureService::ready, &window, [&](const PreparedItem &item) {
        if (!store.addItem(item, &error))
            qWarning() << "写入历史失败:" << error;
        if (window.isVisible())
            window.refresh();
    });

    QObject::connect(&window, &HistoryWindow::entryActivated, &window, [&](qint64 id) {
        Entry entry;
        if (!store.entry(id, &entry))
            return;

        source->suppressNext();
        switch (entry.kind) {
        case ClipKind::Image: {
            const QImage image(entry.path);
            if (image.isNull()) {
                qWarning() << "图片已丢失:" << entry.path;
                return;
            }
            QGuiApplication::clipboard()->setImage(image);
            break;
        }
        case ClipKind::Files: {
            auto *mime = new QMimeData;
            QList<QUrl> urls;
            const QStringList paths = entry.text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &path : paths)
                urls.append(QUrl::fromLocalFile(path));
            mime->setUrls(urls);
            QGuiApplication::clipboard()->setMimeData(mime);
            break;
        }
        case ClipKind::Text:
            QGuiApplication::clipboard()->setText(entry.text);
            break;
        }
    });

    QObject::connect(&tray, &TrayController::pauseToggled, &window,
                     [&](bool value) { paused = value; });
    QObject::connect(&single, &SingleInstance::showRequested, &window, &HistoryWindow::showPanel);

    QObject::connect(&tray, &TrayController::clearRequested, &window, [&] {
        store.clearAll();
        window.refresh();
    });
    QObject::connect(&tray, &TrayController::settingsRequested, &window, [&window] {
        SettingsDialog dialog(&window);
        dialog.exec();
    });
    QObject::connect(&tray, &TrayController::quitRequested, &app, &QApplication::quit);

    source->start();

    if (tray.isAvailable()) {
        tray.show();
    } else {
        qWarning() << "系统托盘不可用，改为直接显示主窗口";
        window.showPanel();
    }

    return app.exec();
}
