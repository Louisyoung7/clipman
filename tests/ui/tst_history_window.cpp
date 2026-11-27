#include <QtTest>

#include <QAbstractItemModel>
#include <QApplication>
#include <QComboBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <memory>

#include "core/Store.h"
#include "ui/FavoriteItemDelegate.h"
#include "ui/HistoryWindow.h"

namespace {

QPoint starPoint(QListWidget *list, int row)
{
    const QRect rect = list->visualRect(list->model()->index(row, 0));
    return QPoint(rect.right() - 8, rect.top() + 16);
}

template <typename Predicate>
void pollUntil(Predicate predicate, int tries = 100)
{
    if (predicate() || tries <= 0)
        return;
    QTimer::singleShot(10, [predicate, tries] { pollUntil(predicate, tries - 1); });
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
    void starShortClickTogglesFavorite();
    void starLongPressEmitsSignal();
    void starLongPressMenuCreatesFolder();
    void folderComboFiltersEntries();

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

void TestHistoryWindow::starShortClickTogglesFavorite()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    auto *list = window.findChild<QListWidget *>();
    QVERIFY(list);
    const qint64 id = m_store->recent().first().id;
    QSignalSpy activatedSpy(&window, &HistoryWindow::entryActivated);

    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, starPoint(list, 0));
    QCOMPARE(activatedSpy.count(), 0);
    QVERIFY(m_store->isFavorite(id));
    QVERIFY(list->item(0)->data(Qt::UserRole + 2).toBool());

    QTest::qWait(50);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, starPoint(list, 0));
    QVERIFY(!m_store->isFavorite(id));
    QVERIFY(!list->item(0)->data(Qt::UserRole + 2).toBool());
}

void TestHistoryWindow::starLongPressEmitsSignal()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    auto *list = window.findChild<QListWidget *>();
    auto *delegate = window.findChild<FavoriteItemDelegate *>();
    QVERIFY(list);
    QVERIFY(delegate);
    // 长按菜单是模态的，这里只验证委托发出的信号。
    QObject::disconnect(delegate, &FavoriteItemDelegate::starLongPressed, &window, nullptr);

    QSignalSpy spy(delegate, &FavoriteItemDelegate::starLongPressed);
    const qint64 id = m_store->recent().first().id;
    const QPoint star = starPoint(list, 0);

    QTest::mousePress(list->viewport(), Qt::LeftButton, {}, star);
    QTest::qWait(600);
    QTest::mouseRelease(list->viewport(), Qt::LeftButton, {}, star);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toLongLong(), id);
    QVERIFY(!m_store->isFavorite(id));
}

void TestHistoryWindow::starLongPressMenuCreatesFolder()
{
    seed({QStringLiteral("alpha")});

    HistoryWindow window(*m_store);
    auto *list = window.findChild<QListWidget *>();
    QVERIFY(list);
    const qint64 id = m_store->recent().first().id;
    const QPoint star = starPoint(list, 0);

    QTimer::singleShot(600, this, [] {
        pollUntil([] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return false;
            for (QAction *action : menu->actions()) {
                if (action->text().contains(QStringLiteral("新建收藏夹"))) {
                    QTimer::singleShot(0, [menu, action] {
                        menu->setActiveAction(action);
                        QTest::keyClick(menu, Qt::Key_Return);
                    });
                    return true;
                }
            }
            menu->close();
            return true;
        });
    });
    QTimer::singleShot(650, this, [] {
        pollUntil([] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (!dialog)
                return false;
            dialog->setTextValue(QStringLiteral("临时收藏"));
            QTimer::singleShot(0, [dialog] { dialog->accept(); });
            return true;
        });
    });
    // 兜底：异常时关闭模态界面，避免测试卡死。
    QTimer::singleShot(4000, this, [] {
        if (QWidget *popup = QApplication::activePopupWidget())
            popup->close();
        if (QWidget *modal = QApplication::activeModalWidget())
            modal->close();
    });

    QTest::mousePress(list->viewport(), Qt::LeftButton, {}, star);
    QTest::qWait(600);
    QTest::mouseRelease(list->viewport(), Qt::LeftButton, {}, star);

    QCOMPARE(m_store->folders().size(), 2);
    QVERIFY(m_store->isFavorite(id));
    const QVector<Folder> folders = m_store->folders();
    QCOMPARE(folders.last().name, QStringLiteral("临时收藏"));
    QVERIFY(m_store->favoriteMap().value(id).contains(folders.last().id));
}

void TestHistoryWindow::folderComboFiltersEntries()
{
    seed({QStringLiteral("alpha"), QStringLiteral("beta")});

    HistoryWindow window(*m_store);
    auto *list = window.findChild<QListWidget *>();
    auto *combo = window.findChild<QComboBox *>();
    QVERIFY(list);
    QVERIFY(combo);

    const qint64 alphaId = m_store->recent().last().id;
    QVERIFY(m_store->setMembership(alphaId, Store::kDefaultFolderId, true));

    QCOMPARE(combo->count(), 2);
    combo->setCurrentIndex(combo->findData(Store::kDefaultFolderId));
    QCOMPARE(list->count(), 1);
    QVERIFY(list->item(0)->data(Qt::UserRole + 2).toBool());

    combo->setCurrentIndex(0);
    QCOMPARE(list->count(), 2);
}

QTEST_MAIN(TestHistoryWindow)

#include "tst_history_window.moc"
