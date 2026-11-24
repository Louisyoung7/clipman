#include <QtTest>

#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "core/Store.h"
#include "ui/HistoryWindow.h"

class TestHistoryWindow : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void refreshListsEntries();
    void searchFiltersEntries();
    void deleteRemovesCurrentEntry();
    void returnEmitsEntryActivated();
    void escapeHidesWindow();

private:
    void seed(const QStringList &texts);

    std::unique_ptr<QTemporaryDir> m_temp;
    std::unique_ptr<Store> m_store;
};

void TestHistoryWindow::init()
{
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());

    m_store = std::make_unique<Store>();
    QVERIFY(m_store->open(m_temp->path() + QStringLiteral("/history.db")));
}

void TestHistoryWindow::cleanup()
{
    m_store.reset();
    m_temp.reset();
}

void TestHistoryWindow::seed(const QStringList &texts)
{
    qint64 createdAt = 1000;
    for (const QString &text : texts) {
        PreparedItem item;
        item.kind = ClipKind::Text;
        item.hash = text;
        item.text = text;
        item.byteSize = text.toUtf8().size();
        item.createdAt = createdAt++;
        item.valid = true;
        QVERIFY(m_store->addItem(item));
    }
}

void TestHistoryWindow::refreshListsEntries()
{
    seed({QStringLiteral("alpha"), QStringLiteral("beta"), QStringLiteral("gamma")});

    HistoryWindow window(*m_store);
    window.refresh();

    auto *list = window.findChild<QListWidget *>();
    QVERIFY(list);
    QCOMPARE(list->count(), 3);
}

void TestHistoryWindow::searchFiltersEntries()
{
    seed({QStringLiteral("alpha one"), QStringLiteral("beta two")});

    HistoryWindow window(*m_store);
    auto *search = window.findChild<QLineEdit *>();
    auto *list = window.findChild<QListWidget *>();
    QVERIFY(search);
    QVERIFY(list);

    search->setText(QStringLiteral("alpha"));
    QCOMPARE(list->count(), 1);
}

void TestHistoryWindow::deleteRemovesCurrentEntry()
{
    seed({QStringLiteral("alpha"), QStringLiteral("beta")});

    HistoryWindow window(*m_store);
    auto *search = window.findChild<QLineEdit *>();
    auto *list = window.findChild<QListWidget *>();
    QVERIFY(search);
    QVERIFY(list);

    QTest::keyClick(search, Qt::Key_Delete);

    QCOMPARE(m_store->count(), 1);
    QCOMPARE(list->count(), 1);
}

void TestHistoryWindow::returnEmitsEntryActivated()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    auto *search = window.findChild<QLineEdit *>();
    QVERIFY(search);

    QSignalSpy spy(&window, &HistoryWindow::entryActivated);
    QTest::keyClick(search, Qt::Key_Return);

    QCOMPARE(spy.count(), 1);
    QVERIFY(window.isHidden());
}

void TestHistoryWindow::escapeHidesWindow()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    auto *search = window.findChild<QLineEdit *>();
    QVERIFY(search);

    window.show();
    QTest::keyClick(search, Qt::Key_Escape);

    QVERIFY(window.isHidden());
}

QTEST_MAIN(TestHistoryWindow)

#include "tst_history_window.moc"
