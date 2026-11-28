#include "platform/SingleInstance.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>
#include <QLockFile>

namespace {

constexpr auto kServiceName = "org.clipory.Clipory";
constexpr auto kObjectPath = "/Clipory";

} // namespace

SingleInstance::SingleInstance(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
}

SingleInstance::~SingleInstance()
{
    if (m_dbusRegistered) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterObject(QString::fromLatin1(kObjectPath));
        bus.unregisterService(QString::fromLatin1(kServiceName));
    }
}

bool SingleInstance::tryBecomePrimary()
{
    // 先抢本地锁：即使两个自启项同时拉起两个进程，也只有一个走到初始化与 grab，
    // 另一个直接通知已有实例后退出。
    m_lock = std::make_unique<QLockFile>(m_dataDir + QStringLiteral("/clipory.lock"));
    if (!m_lock->tryLock(0))
        return false;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return true; // 无 session bus（精简环境）：本地锁就是最终判据

    if (!bus.registerObject(QString::fromLatin1(kObjectPath), this,
                            QDBusConnection::ExportAllSlots)) {
        m_lock->unlock();
        return false;
    }
    if (!bus.registerService(QString::fromLatin1(kServiceName))) {
        bus.unregisterObject(QString::fromLatin1(kObjectPath));
        m_lock->unlock();
        return false;
    }

    m_dbusRegistered = true;
    return true;
}

bool SingleInstance::notifyExisting()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;

    QDBusMessage message = QDBusMessage::createMethodCall(
        QString::fromLatin1(kServiceName), QString::fromLatin1(kObjectPath),
        QString::fromLatin1(kServiceName), QStringLiteral("show"));
    const QDBusMessage reply = bus.call(message, QDBus::Block, 1500);
    if (reply.type() == QDBusMessage::ErrorMessage)
        qWarning() << "唤醒已有实例失败:" << reply.errorMessage();
    return reply.type() == QDBusMessage::ReplyMessage;
}

void SingleInstance::show()
{
    emit showRequested();
}
