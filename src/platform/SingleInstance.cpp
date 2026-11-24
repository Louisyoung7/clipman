#include "platform/SingleInstance.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDebug>
#include <QLockFile>

namespace {

constexpr auto kServiceName = "org.clipman.Clipman";
constexpr auto kObjectPath = "/Clipman";

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
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        if (bus.registerObject(QString::fromLatin1(kObjectPath), this,
                               QDBusConnection::ExportAllSlots)) {
            if (bus.registerService(QString::fromLatin1(kServiceName))) {
                m_dbusRegistered = true;
                return true;
            }
            bus.unregisterObject(QString::fromLatin1(kObjectPath));
        }
        return false;
    }

    // 无 session bus（精简环境/无桌面会话）时退回锁文件。
    m_lock = std::make_unique<QLockFile>(m_dataDir + QStringLiteral("/clipman.lock"));
    return m_lock->tryLock(0);
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
