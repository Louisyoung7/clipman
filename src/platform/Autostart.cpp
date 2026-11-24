#include "platform/Autostart.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace {

constexpr auto kSystemAutostartFile = "/etc/xdg/autostart/clipman.desktop";

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QByteArray desktopFile(bool enabled)
{
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=Clipman\n"
                          "Comment=剪贴板历史工具\n"
                          "Exec=clipman\n"
                          "Icon=clipman\n"
                          "Terminal=false\n"
                          "NoDisplay=true\n"
                          "Hidden=%1\n"
                          "X-GNOME-Autostart-enabled=%2\n")
        .arg(enabled ? QStringLiteral("false") : QStringLiteral("true"),
             enabled ? QStringLiteral("true") : QStringLiteral("false"))
        .toUtf8();
}

} // namespace

Autostart::Autostart(QString userDir, QString systemFile)
{
    if (userDir.isEmpty())
        userDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
                  QStringLiteral("/autostart");
    m_userFile = userDir + QStringLiteral("/clipman.desktop");
    m_systemFile = systemFile.isEmpty() ? QString::fromLatin1(kSystemAutostartFile) : systemFile;
}

bool Autostart::isEnabled() const
{
    if (QFileInfo::exists(m_userFile)) {
        QSettings settings(m_userFile, QSettings::IniFormat);
        return !settings.value(QStringLiteral("Desktop Entry/Hidden"), false).toBool();
    }
    return QFileInfo::exists(m_systemFile);
}

bool Autostart::setEnabled(bool enabled, QString *error)
{
    // 没有系统级条目时，关闭直接删除用户文件，避免留下无意义的 Hidden=true。
    if (!enabled && !QFileInfo::exists(m_systemFile)) {
        if (!QFileInfo::exists(m_userFile))
            return true;
        if (!QFile::remove(m_userFile)) {
            setError(error, QStringLiteral("无法删除自启文件: %1").arg(m_userFile));
            return false;
        }
        return true;
    }

    const QFileInfo info(m_userFile);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(error, QStringLiteral("无法创建自启目录: %1").arg(info.absolutePath()));
        return false;
    }

    QSaveFile file(m_userFile);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, file.errorString());
        return false;
    }

    const QByteArray content = desktopFile(enabled);
    if (file.write(content) != content.size() || !file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}
