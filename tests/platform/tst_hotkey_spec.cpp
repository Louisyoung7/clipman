#include <QtTest>

#include "platform/HotkeySpec.h"

class TestHotkeySpec : public QObject
{
    Q_OBJECT

private slots:
    void parsesDefaultSequence();
    void rejectsPlainKey();
    void rejectsShiftOnly();
    void acceptsShiftCombo();
    void rejectsMultiChord();
    void mapsFunctionKey();
    void rejectsUnknownKey();
    void roundTripsToString();
};

void TestHotkeySpec::parsesDefaultSequence()
{
    HotkeySpec spec;
    QString error;
    QVERIFY2(HotkeySpec::parse(QStringLiteral("Ctrl+Alt+V"), &spec, &error), qPrintable(error));
    QCOMPARE(spec.key, int(Qt::Key_V));
    QVERIFY(spec.modifiers.testFlag(Qt::ControlModifier));
    QVERIFY(spec.modifiers.testFlag(Qt::AltModifier));
    QCOMPARE(x11KeysymForQtKey(spec.key), quint32(0x76));
}

void TestHotkeySpec::rejectsPlainKey()
{
    HotkeySpec spec;
    QString error;
    QVERIFY(!HotkeySpec::parse(QStringLiteral("V"), &spec, &error));
    QVERIFY(error.contains(QStringLiteral("Ctrl")));
}

void TestHotkeySpec::rejectsShiftOnly()
{
    HotkeySpec spec;
    QVERIFY(!HotkeySpec::parse(QStringLiteral("Shift+V"), &spec));
}

void TestHotkeySpec::acceptsShiftCombo()
{
    HotkeySpec spec;
    QString error;
    QVERIFY2(HotkeySpec::parse(QStringLiteral("Ctrl+Shift+Space"), &spec, &error),
             qPrintable(error));
    QCOMPARE(spec.key, int(Qt::Key_Space));
    QVERIFY(spec.modifiers.testFlag(Qt::ShiftModifier));
    QCOMPARE(x11KeysymForQtKey(spec.key), quint32(0x20));
}

void TestHotkeySpec::rejectsMultiChord()
{
    HotkeySpec spec;
    QVERIFY(!HotkeySpec::parse(QStringLiteral("Ctrl+Alt+V, Ctrl+Alt+W"), &spec));
}

void TestHotkeySpec::mapsFunctionKey()
{
    HotkeySpec spec;
    QVERIFY(HotkeySpec::parse(QStringLiteral("Ctrl+Alt+F5"), &spec));
    QCOMPARE(x11KeysymForQtKey(spec.key), quint32(0xffc2));
}

void TestHotkeySpec::rejectsUnknownKey()
{
    HotkeySpec spec;
    QVERIFY(!HotkeySpec::parse(QString(), &spec));
    QVERIFY(!HotkeySpec::parse(QStringLiteral("Ctrl+Alt+Multi_key"), &spec));
}

void TestHotkeySpec::roundTripsToString()
{
    HotkeySpec spec;
    QString error;
    QVERIFY2(HotkeySpec::parse(QStringLiteral("Ctrl+Alt+V"), &spec, &error), qPrintable(error));

    HotkeySpec reparsed;
    QVERIFY2(HotkeySpec::parse(spec.toString(), &reparsed, &error), qPrintable(error));
    QCOMPARE(reparsed.key, spec.key);
    QVERIFY(reparsed.modifiers == spec.modifiers);
}

QTEST_GUILESS_MAIN(TestHotkeySpec)

#include "tst_hotkey_spec.moc"
