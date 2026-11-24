#include <QtTest>

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
    void disableWritesHiddenEntry();
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

void TestAutostart::disableWritesHiddenEntry()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    Autostart autostart(temp.path() + QStringLiteral("/config"),
                        temp.path() + QStringLiteral("/system/clipman.desktop"));
    QVERIFY(autostart.setEnabled(true));
    QVERIFY(autostart.setEnabled(false));

    QSettings settings(autostart.userFilePath(), QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("Desktop Entry/Hidden")).toString(),
             QStringLiteral("true"));
    QVERIFY(!autostart.isEnabled());
}

QTEST_MAIN(TestAutostart)

#include "tst_autostart.moc"
