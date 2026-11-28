#include <QtTest>

#include <QAbstractItemModel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "core/Store.h"
#include "ui/HistoryWindow.h"

namespace {

QPoint starPoint(QListWidget *list, int row)
{
    const QRect rect = list->visualRect(list->model()->index(row, 0));
    const int right = qMin(rect.right(), list->width() - 1);
    return QPoint(right - 16, rect.top() + 16);
}

QPushButton *tabButton(HistoryWindow &window, const QString &text)
{
    for (QPushButton *button : window.findChildren<QPushButton *>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

} // namespace

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
    void starClickTogglesFavorite();
    void rapidStarClicksKeepWindowVisible();
    void favoriteTabShowsOnlyFavorites();
    void toggleShowsPanelAfterExternalHide();
    void showPanelTwiceStaysVisible();
    void showPanelFocusesSearch();

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

void TestHistoryWindow::starClickTogglesFavorite()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    window.show();
    auto *list = window.findChild<QListWidget *>();
    QVERIFY(list);
    const qint64 id = m_store->recent().first().id;
    QSignalSpy activatedSpy(&window, &HistoryWindow::entryActivated);

    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, starPoint(list, 0));
    QCOMPARE(activatedSpy.count(), 0);
    QVERIFY(m_store->isFavorite(id));
    QVERIFY(list->item(0)->data(Qt::UserRole + 2).toBool());
    QVERIFY(window.isVisible());

    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, starPoint(list, 0));
    QCOMPARE(activatedSpy.count(), 0);
    QVERIFY(!m_store->isFavorite(id));
    QVERIFY(!list->item(0)->data(Qt::UserRole + 2).toBool());
    QVERIFY(window.isVisible());
}

void TestHistoryWindow::rapidStarClicksKeepWindowVisible()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    window.show();
    auto *list = window.findChild<QListWidget *>();
    QVERIFY(list);
    QSignalSpy activatedSpy(&window, &HistoryWindow::entryActivated);

    for (int i = 0; i < 4; ++i) {
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, starPoint(list, 0));
        QVERIFY(window.isVisible());
    }
    QTest::mouseDClick(list->viewport(), Qt::LeftButton, {}, starPoint(list, 0));
    QVERIFY(window.isVisible());
    QCOMPARE(activatedSpy.count(), 0);
}

void TestHistoryWindow::favoriteTabShowsOnlyFavorites()
{
    seed({QStringLiteral("alpha"), QStringLiteral("beta")});

    HistoryWindow window(*m_store);
    auto *list = window.findChild<QListWidget *>();
    QVERIFY(list);
    const qint64 alphaId = m_store->recent().last().id;
    QVERIFY(m_store->setMembership(alphaId, Store::kDefaultFolderId, true));

    auto *historyTab = tabButton(window, QStringLiteral("历史"));
    auto *favoriteTab = tabButton(window, QStringLiteral("收藏"));
    QVERIFY(historyTab);
    QVERIFY(favoriteTab);

    favoriteTab->click();
    QCOMPARE(list->count(), 1);
    QVERIFY(list->item(0)->text().contains(QStringLiteral("alpha")));

    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, starPoint(list, 0));
    QCOMPARE(list->count(), 0);

    historyTab->click();
    QCOMPARE(list->count(), 2);
}

void TestHistoryWindow::toggleShowsPanelAfterExternalHide()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    window.showPanel();
    QTRY_VERIFY(window.isVisible());

    // 模拟旧实现里的坏状态：面板被外部（Qt::Popup 自动关闭）收敛成隐藏，
    // 但状态标记没对齐。下一次唤起必须一次就能显示出来。
    window.hide();
    QVERIFY(window.isHidden());

    window.togglePanel();
    QTRY_VERIFY(window.isVisible());
}

void TestHistoryWindow::showPanelTwiceStaysVisible()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    window.showPanel();
    window.showPanel();

    QVERIFY(window.isVisible());
}

void TestHistoryWindow::showPanelFocusesSearch()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    auto *search = window.findChild<QLineEdit *>();
    QVERIFY(search);

    window.showPanel();
    QTRY_VERIFY(search->hasFocus());
}

QTEST_MAIN(TestHistoryWindow)

#include "tst_history_window.moc"
