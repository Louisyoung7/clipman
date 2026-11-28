#pragma once

#include <QString>

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

private:
    QString m_userFile;
    QString m_systemFile;
};
