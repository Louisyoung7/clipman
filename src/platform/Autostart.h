#pragma once

#include <QString>
#include <QStringList>

// 用户级开机自启开关：写 ~/.config/autostart/clipory.desktop 覆盖
// /etc/xdg/autostart 里的系统级条目（Hidden=true 即屏蔽）。只依赖 QtCore。
class Autostart
{
public:
    // 空参数表示使用默认位置；测试可注入临时目录与系统文件路径。
    explicit Autostart(QString userDir = QString(), QString systemFile = QString());

    bool isEnabled() const;
    bool setEnabled(bool enabled, QString *error = nullptr);

    QString userFilePath() const { return m_userFile; }

    // 启动时清理与系统级条目重复的用户级条目：旧版本在没有系统条目时会写
    // ~/.config/autostart/clipory.desktop，如今两者并存会让同一会话启动两次。
    // 用户在设置里显式关过自启（Hidden=true）时保留，那是有意义的屏蔽项。
    static void removeRedundantUserEntry(const QString &userDir = QString(),
                                         const QStringList &systemFiles = QStringList());

    // 系统级自启条目的候选路径（deb 安装的正式名字在前，兼容旧名），供测试与诊断使用。
    static QStringList systemAutostartFiles();

private:
    static QString defaultSystemFile();

    QString m_userFile;
    QString m_systemFile;
};
