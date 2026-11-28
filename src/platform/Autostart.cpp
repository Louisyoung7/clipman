#include "platform/Autostart.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace {

// deb 安装的是 clipory-autostart.desktop（见 CMakeLists.txt 的 install 规则）；
// 旧版本/手工安装可能留下 clipory.desktop，两者都认。
QStringList candidateFiles()
{
    return {QStringLiteral("/etc/xdg/autostart/clipory-autostart.desktop"),
            QStringLiteral("/etc/xdg/autostart/clipory.desktop")};
}

bool anyExists(const QStringList &files)
{
    for (const QString &path : files) {
        if (QFileInfo::exists(path))
            return true;
    }
    return false;
}

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QByteArray desktopFile(bool enabled)
{
    return QStringLiteral("[Desktop Entry]\n"
                          "Type=Application\n"
                          "Name=Clipory\n"
                          "Comment=剪贴板历史工具\n"
                          "Exec=clipory\n"
                          "Icon=clipory\n"
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
    m_userFile = userDir + QStringLiteral("/clipory.desktop");
    m_systemFile = systemFile.isEmpty() ? defaultSystemFile() : systemFile;
}

QString Autostart::defaultSystemFile()
{
    for (const QString &path : systemAutostartFiles()) {
        if (QFileInfo::exists(path))
            return path;
    }
    // 都没安装时返回约定的正式名字，isEnabled() 会因文件不存在而为 false。
    return systemAutostartFiles().first();
}

QStringList Autostart::systemAutostartFiles()
{
    return candidateFiles();
}

void Autostart::removeRedundantUserEntry(const QString &userDir, const QStringList &systemFiles)
{
    const Autostart autostart(userDir);
    if (!anyExists(systemFiles.isEmpty() ? systemAutostartFiles() : systemFiles) ||
        !QFileInfo::exists(autostart.m_userFile))
        return;

    const QSettings settings(autostart.m_userFile, QSettings::IniFormat);
    if (settings.value(QStringLiteral("Desktop Entry/Hidden"), false).toBool())
        return; // 用户显式关过自启，这条屏蔽项要留着

    if (!QFile::remove(autostart.m_userFile))
        qWarning() << "清理重复的开机自启项失败:" << autostart.m_userFile;
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
