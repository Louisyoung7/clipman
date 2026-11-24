#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "core/Entry.h"
#include "core/Store.h"

namespace {

PreparedItem textItem(const QString &hash, const QString &text, qint64 createdAt = 0)
{
    PreparedItem item;
    item.kind = ClipKind::Text;
    item.hash = hash;
    item.text = text;
    item.byteSize = text.toUtf8().size();
    item.createdAt = createdAt;
    item.valid = true;
    return item;
}

PreparedItem imageItem(const QString &hash, const QString &path)
{
    PreparedItem item;
    item.kind = ClipKind::Image;
    item.hash = hash;
    item.mime = QStringLiteral("image/png");
    item.path = path;
    item.width = 4;
    item.height = 3;
    item.byteSize = 8;
    item.valid = true;
    return item;
}

int userVersion(const QString &dbPath)
{
    int version = -1;
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("version_probe"));
        db.setDatabaseName(dbPath);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
                version = query.value(0).toInt();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("version_probe"));
    return version;
}

void createV1Database(const QString &dbPath)
{
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("v1_setup"));
        db.setDatabaseName(dbPath);
        REQUIRE(db.open());

        QSqlQuery query(db);
        REQUIRE(query.exec(QStringLiteral("CREATE TABLE entries ("
                                          "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                          "  hash TEXT NOT NULL UNIQUE,"
                                          "  text_content TEXT,"
                                          "  byte_size INTEGER NOT NULL DEFAULT 0,"
                                          "  created_at INTEGER NOT NULL"
                                          ")")));
        REQUIRE(query.exec(
            QStringLiteral("CREATE INDEX idx_entries_created_at ON entries(created_at DESC)")));
        REQUIRE(query.exec(
            QStringLiteral("INSERT INTO entries(hash, text_content, byte_size, created_at) "
                           "VALUES('legacy-hash', 'legacy text', 11, 1000)")));
        REQUIRE(query.exec(QStringLiteral("PRAGMA user_version = 1")));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("v1_setup"));
}

} // namespace

TEST_CASE("Store::open 建立 v3 schema", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    const QString dbPath = temp.path() + QStringLiteral("/history.db");
    Store store;
    QString error;
    REQUIRE(store.open(dbPath, &error));
    REQUIRE(userVersion(dbPath) == 3);
    REQUIRE(store.count() == 0);
}

TEST_CASE("Store::open 把 v1 库迁移到 v3", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    const QString dbPath = temp.path() + QStringLiteral("/history.db");
    createV1Database(dbPath);

    Store store;
    QString error;
    REQUIRE(store.open(dbPath, &error));
    REQUIRE(userVersion(dbPath) == 3);
    REQUIRE(store.count() == 1);

    const QVector<Entry> entries = store.recent();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.first().text == QStringLiteral("legacy text"));
    REQUIRE(entries.first().kind == ClipKind::Text);
}

TEST_CASE("Store::addItem 按 hash 去重并刷新时间", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));

    REQUIRE(store.addItem(textItem(QStringLiteral("h1"), QStringLiteral("hello"), 1000)));
    REQUIRE(store.addItem(textItem(QStringLiteral("h1"), QStringLiteral("hello"), 2000)));
    REQUIRE(store.count() == 1);

    Entry entry;
    REQUIRE(store.entry(store.recent().first().id, &entry));
    REQUIRE(entry.createdAt == 2000);
}

TEST_CASE("Store::search 使用 FTS5 前缀查询", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));
    REQUIRE(store.addItem(textItem(QStringLiteral("h1"), QStringLiteral("hello world"), 1000)));
    REQUIRE(store.addItem(textItem(QStringLiteral("h2"), QStringLiteral("goodbye"), 2000)));

    const QVector<Entry> hits = store.search(QStringLiteral("hell"));
    REQUIRE(hits.size() == 1);
    REQUIRE(hits.first().text == QStringLiteral("hello world"));

    REQUIRE(store.search(QString()).size() == store.recent().size());
}

TEST_CASE("Store::removeEntry 删除图片时清理 blob", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));

    const QString blobPath =
        store.blobStore().saveImage(QStringLiteral("img1"), QByteArray("png-data"));
    REQUIRE(!blobPath.isEmpty());
    REQUIRE(store.addItem(imageItem(QStringLiteral("img1"), blobPath)));
    REQUIRE(QFile::exists(blobPath));

    REQUIRE(store.removeEntry(store.recent().first().id));
    REQUIRE(store.count() == 0);
    REQUIRE(!QFile::exists(blobPath));
}

TEST_CASE("Store::clearAll 清空记录与图片文件", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));

    const QString blobPath =
        store.blobStore().saveImage(QStringLiteral("img1"), QByteArray("png-data"));
    REQUIRE(store.addItem(textItem(QStringLiteral("h1"), QStringLiteral("hello"))));
    REQUIRE(store.addItem(imageItem(QStringLiteral("img1"), blobPath)));

    REQUIRE(store.clearAll());
    REQUIRE(store.count() == 0);
    REQUIRE(QDir(store.blobStore().dir()).entryList(QDir::Files).isEmpty());
}

TEST_CASE("Store 裁剪到最新的 1000 条", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));

    for (int i = 0; i < 1005; ++i) {
        const QString hash = QStringLiteral("h%1").arg(i);
        REQUIRE(store.addItem(textItem(hash, QStringLiteral("text %1").arg(i), 1000 + i)));
    }

    REQUIRE(store.count() == 1000);
    REQUIRE(store.recent(1).first().hash == QStringLiteral("h1004"));

    const QVector<Entry> entries = store.recent(1000);
    for (const Entry &entry : entries)
        REQUIRE(entry.hash != QStringLiteral("h0"));
}
