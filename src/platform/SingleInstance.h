#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QLockFile;

// 单实例控制：优先用 session D-Bus 注册服务并暴露 show()；
// 没有 session bus 时回退到 QLockFile。
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
