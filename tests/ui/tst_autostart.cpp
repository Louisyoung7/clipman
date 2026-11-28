#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

#include "platform/Autostart.h"

class TestAutostart : public QObject
{
    Q_OBJECT

private slots:
    void disabledWhenNoFiles();
    void enableWritesVisibleEntry();
    void disableRemovesEntryWithoutSystemFile();
    void disableHidesSystemEntry();
    void systemCandidatesPreferDebName();
    void removesRedundantUserEntry();
    void keepsDisabledUserEntry();
    void keepsUserEntryWithoutSystemFile();
};

void TestAutostart::disabledWhenNoFiles()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    Autostart autostart(temp.path() + QStringLiteral("/config"),
                        temp.path() + QStringLiteral("/system/clipory.desktop"));
    QVERIFY(!autostart.isEnabled());
}

void TestAutostart::enableWritesVisibleEntry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    Autostart autostart(temp.path() + QStringLiteral("/config"),
                        temp.path() + QStringLiteral("/system/clipory.desktop"));
    QVERIFY(autostart.setEnabled(true));
    QVERIFY(QFileInfo::exists(autostart.userFilePath()));

    QSettings settings(autostart.userFilePath(), QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("Desktop Entry/Hidden")).toString(),
             QStringLiteral("false"));
    QVERIFY(autostart.isEnabled());
}

void TestAutostart::disableRemovesEntryWithoutSystemFile()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    Autostart autostart(temp.path() + QStringLiteral("/config"),
                        temp.path() + QStringLiteral("/system/clipory.desktop"));
    QVERIFY(autostart.setEnabled(true));
    QVERIFY(autostart.setEnabled(false));

    QVERIFY(!QFileInfo::exists(autostart.userFilePath()));
    QVERIFY(!autostart.isEnabled());
}

void TestAutostart::disableHidesSystemEntry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString systemFile = temp.path() + QStringLiteral("/system/clipory.desktop");
    QVERIFY(QDir().mkpath(QFileInfo(systemFile).absolutePath()));
    QFile file(systemFile);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[Desktop Entry]\nType=Application\n");
    file.close();

    Autostart autostart(temp.path() + QStringLiteral("/config"), systemFile);
    QVERIFY(autostart.isEnabled());
    QVERIFY(autostart.setEnabled(false));

    QSettings settings(autostart.userFilePath(), QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("Desktop Entry/Hidden")).toString(),
             QStringLiteral("true"));
    QVERIFY(!autostart.isEnabled());
}

// 系统级条目探针：在临时目录造一份，模拟已安装的系统自启项。
namespace {

QString makeSystemEntry(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write("[Desktop Entry]\nType=Application\nExec=clipory\n");
    file.close();
    return path;
}

} // namespace

void TestAutostart::systemCandidatesPreferDebName()
{
    // deb 实际安装的是 clipory-autostart.desktop，必须排在候选首位，
    // 否则设置里的开关会误判为“未启用”，关自启也关不掉。
    const QStringList files = Autostart::systemAutostartFiles();
    QVERIFY(!files.isEmpty());
    QCOMPARE(files.first(), QStringLiteral("/etc/xdg/autostart/clipory-autostart.desktop"));
}

void TestAutostart::removesRedundantUserEntry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString systemFile =
        makeSystemEntry(temp.path() + QStringLiteral("/system/clipory-autostart.desktop"));
    QVERIFY(!systemFile.isEmpty());

    const QString userDir = temp.path() + QStringLiteral("/config");
    Autostart autostart(userDir, systemFile);
    QVERIFY(autostart.setEnabled(true));
    QVERIFY(QFileInfo::exists(autostart.userFilePath()));

    Autostart::removeRedundantUserEntry(userDir, {systemFile});
    QVERIFY(!QFileInfo::exists(autostart.userFilePath()));
}

void TestAutostart::keepsDisabledUserEntry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString systemFile =
        makeSystemEntry(temp.path() + QStringLiteral("/system/clipory-autostart.desktop"));
    QVERIFY(!systemFile.isEmpty());

    const QString userDir = temp.path() + QStringLiteral("/config");
    Autostart autostart(userDir, systemFile);
    QVERIFY(autostart.setEnabled(true));
    QVERIFY(autostart.setEnabled(false));

    Autostart::removeRedundantUserEntry(userDir, {systemFile});
    QVERIFY(QFileInfo::exists(autostart.userFilePath()));
    QVERIFY(!autostart.isEnabled());
}

void TestAutostart::keepsUserEntryWithoutSystemFile()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString userDir = temp.path() + QStringLiteral("/config");
    Autostart autostart(userDir,
                        temp.path() + QStringLiteral("/system/clipory-autostart.desktop"));
    QVERIFY(autostart.setEnabled(true));

    Autostart::removeRedundantUserEntry(userDir,
                                        {temp.path() + QStringLiteral("/system/missing.desktop")});
    QVERIFY(QFileInfo::exists(autostart.userFilePath()));
    QVERIFY(autostart.isEnabled());
}

QTEST_MAIN(TestAutostart)

#include "tst_autostart.moc"
