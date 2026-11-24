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
};

void TestAutostart::disabledWhenNoFiles()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    Autostart autostart(temp.path() + QStringLiteral("/config"),
                        temp.path() + QStringLiteral("/system/clipman.desktop"));
    QVERIFY(!autostart.isEnabled());
}

void TestAutostart::enableWritesVisibleEntry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    Autostart autostart(temp.path() + QStringLiteral("/config"),
                        temp.path() + QStringLiteral("/system/clipman.desktop"));
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
                        temp.path() + QStringLiteral("/system/clipman.desktop"));
    QVERIFY(autostart.setEnabled(true));
    QVERIFY(autostart.setEnabled(false));

    QVERIFY(!QFileInfo::exists(autostart.userFilePath()));
    QVERIFY(!autostart.isEnabled());
}

void TestAutostart::disableHidesSystemEntry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString systemFile = temp.path() + QStringLiteral("/system/clipman.desktop");
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

QTEST_MAIN(TestAutostart)

#include "tst_autostart.moc"
