#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QLockFile;

// 单实例控制：先用 QLockFile 抢占数据目录（系统级与用户级自启可能同一毫秒启动两个进程，
// 锁保证只有一个进入初始化与快捷键注册），再注册 session D-Bus 服务暴露 show()，
// 供第二次启动唤醒已运行实例；没有 session bus 时本地锁即最终判据。
class SingleInstance : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.clipory.Clipory")

public:
    explicit SingleInstance(const QString &dataDir, QObject *parent = nullptr);
    ~SingleInstance() override;

    bool tryBecomePrimary();
    bool notifyExisting();

signals:
    void showRequested();

public slots:
    void show();

private:
    QString m_dataDir;
    std::unique_ptr<QLockFile> m_lock;
    bool m_dbusRegistered = false;
};
