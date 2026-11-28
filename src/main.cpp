#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QMessageBox>
#include <QMimeData>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include <memory>

#include "core/CaptureService.h"
#include "core/Store.h"
#include "platform/ClipboardSource.h"
#include "platform/ClipboardWatcher.h"
#include "platform/HotkeySpec.h"
#include "platform/SingleInstance.h"
#include "platform/X11Hotkey.h"
#include "platform/XcbClipboardWatcher.h"
#include "ui/HistoryWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/TrayController.h"

namespace {

// 旧版本以 clipman 同时作为组织名与应用名；改名后把已有数据搬到 clipory 路径，
// 避免用户历史与配置丢失。只在目标不存在时迁移，失败时保留原目录并继续启动。
void migrateLegacyData()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    const QString dataRoot =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);

    const QString oldConfig = configRoot + QStringLiteral("/clipman");
    const QString newConfig = configRoot + QStringLiteral("/clipory");
    if (QFileInfo::exists(oldConfig) && !QFileInfo::exists(newConfig)) {
        if (!QDir().rename(oldConfig, newConfig)) {
            qWarning() << "迁移旧配置目录失败:" << oldConfig;
        } else {
            const QString oldSettings = newConfig + QStringLiteral("/clipman.conf");
            const QString newSettings = newConfig + QStringLiteral("/clipory.conf");
            if (QFileInfo::exists(oldSettings) && !QFileInfo::exists(newSettings) &&
                !QFile::rename(oldSettings, newSettings))
                qWarning() << "迁移旧配置文件失败:" << oldSettings;
        }
    }

    const QString oldData = dataRoot + QStringLiteral("/clipman/clipman");
    const QString newData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (QFileInfo::exists(oldData) && !QFileInfo::exists(newData)) {
        if (!QDir().mkpath(QFileInfo(newData).absolutePath()) || !QDir().rename(oldData, newData))
            qWarning() << "迁移旧数据目录失败:" << oldData;
    }

    const QString oldAutostart = configRoot + QStringLiteral("/autostart/clipman.desktop");
    const QString newAutostart = configRoot + QStringLiteral("/autostart/clipory.desktop");
    if (QFileInfo::exists(oldAutostart) && !QFileInfo::exists(newAutostart)) {
        QFile file(oldAutostart);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray content = file.readAll();
            file.close();
            content.replace("clipman", "clipory");
            QSaveFile saved(newAutostart);
            if (saved.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                saved.write(content) == content.size() && saved.commit())
                QFile::remove(oldAutostart);
            else
                qWarning() << "迁移旧自启项失败:" << oldAutostart;
        }
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("clipory"));
    QApplication::setApplicationName(QStringLiteral("clipory"));
    QApplication::setApplicationVersion(QStringLiteral(CLIPORY_VERSION));
    QApplication::setApplicationDisplayName(QStringLiteral("Clipory"));
    QApplication::setDesktopFileName(QStringLiteral("clipory"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/clipory.svg")));
    QApplication::setQuitOnLastWindowClosed(false);

    migrateLegacyData();

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
        QMessageBox::critical(nullptr, QStringLiteral("Clipory"),
                              QStringLiteral("打开历史数据库失败：%1").arg(error));
        return 1;
    }

    QSettings settings;
    store.setMaxEntries(
        settings.value(QStringLiteral("history/maxEntries"), Store::kDefaultRetention).toInt());

    HistoryWindow window(store);
    CaptureService capture(store.blobStore());

    std::unique_ptr<ClipboardSource> source = XcbClipboardWatcher::create();
    if (!source) {
        qWarning() << "XFixes 不可用，回退到 QClipboard 监听";
        source = std::make_unique<ClipboardWatcher>(QGuiApplication::clipboard());
    }

    TrayController tray(&window);

    std::unique_ptr<X11Hotkey> hotkey = X11Hotkey::create();
    std::unique_ptr<X11Hotkey> settingsHotkey = X11Hotkey::create();

    auto applyTo = [](X11Hotkey *target, const QString &sequence, QString *hotkeyError) -> bool {
        if (!target) {
            if (hotkeyError)
                *hotkeyError = QStringLiteral("当前环境不支持 X11 全局快捷键");
            return false;
        }
        HotkeySpec spec;
        if (!HotkeySpec::parse(sequence, &spec, hotkeyError))
            return false;
        return target->apply(spec, hotkeyError);
    };

    auto applyHotkey = [&](const QString &sequence, QString *hotkeyError) -> bool {
        return applyTo(hotkey.get(), sequence, hotkeyError);
    };

    auto applySettingsHotkey = [&](const QString &sequence, QString *hotkeyError) -> bool {
        return applyTo(settingsHotkey.get(), sequence, hotkeyError);
    };

    auto readSequence = [](const char *key, const char *fallback) {
        return QSettings()
            .value(QString::fromLatin1(key), QString::fromLatin1(fallback))
            .toString();
    };

    const QString savedHotkey = readSequence(kHotkeySequenceKey, kDefaultHotkeySequence);
    QString hotkeyError;
    if (!applyHotkey(savedHotkey, &hotkeyError))
        qWarning() << "全局快捷键不可用:" << hotkeyError; // 不静默改写用户配置
    if (hotkey)
        QObject::connect(hotkey.get(), &X11Hotkey::activated, &window, &HistoryWindow::togglePanel);

    QString settingsHotkeyError;
    if (!applySettingsHotkey(readSequence(kSettingsHotkeySequenceKey, kDefaultSettingsHotkeySequence),
                             &settingsHotkeyError))
        qWarning() << "设置快捷键不可用:" << settingsHotkeyError;

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

        // 历史项被重新使用（回填剪贴板）后按最新时间参与排序与裁剪。
        if (!store.touchEntry(id, &error))
            qWarning() << "刷新历史时间失败:" << error;
    });

    QObject::connect(&tray, &TrayController::pauseToggled, &window,
                     [&](bool value) { paused = value; });
    QObject::connect(&single, &SingleInstance::showRequested, &window, &HistoryWindow::showPanel);

    QObject::connect(&tray, &TrayController::clearRequested, &window, [&] {
        store.clearAll();
        window.refresh();
    });
    auto openSettings = [&] {
        // 已有模态窗口（设置界面）时忽略重复触发，避免叠出多个对话框。
        if (QApplication::activeModalWidget())
            return;

        // 先释放两个全局 grab，否则录制快捷键时按键仍会被抢走。
        if (hotkey)
            hotkey->release();
        if (settingsHotkey)
            settingsHotkey->release();
        if (window.isVisible())
            window.hide();

        // 不设置父窗口：GNOME 的 attach-modal-dialogs 会把模态对话框
        // 贴到父窗口所在屏幕，多屏时容易跑到副屏。位置由对话框自身按鼠标定位。
        SettingsDialog dialog(nullptr, applyHotkey, applySettingsHotkey);
        dialog.exec();

        // 关闭后按已保存的配置恢复两个全局快捷键（对话框内可能刚应用过新键）。
        QString restoreError;
        if (!applyHotkey(readSequence(kHotkeySequenceKey, kDefaultHotkeySequence), &restoreError))
            qWarning() << "全局快捷键恢复失败:" << restoreError;

        QString settingsRestoreError;
        if (!applySettingsHotkey(
                readSequence(kSettingsHotkeySequenceKey, kDefaultSettingsHotkeySequence),
                &settingsRestoreError))
            qWarning() << "设置快捷键恢复失败:" << settingsRestoreError;

        const int retention = QSettings()
                                  .value(QStringLiteral("history/maxEntries"),
                                         Store::kDefaultRetention)
                                  .toInt();
        if (retention != store.maxEntries()) {
            store.setMaxEntries(retention);
            window.refresh();
        }
    };
    QObject::connect(&tray, &TrayController::settingsRequested, &window, openSettings);
    if (settingsHotkey)
        QObject::connect(settingsHotkey.get(), &X11Hotkey::activated, &window, openSettings);
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
