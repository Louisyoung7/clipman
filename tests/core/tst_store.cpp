#include <catch2/catch_test_macros.hpp>

#include <QDateTime>
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

qint64 entryIdOf(const Store &store, const QString &hash)
{
    const QVector<Entry> entries = store.recent();
    for (const Entry &entry : entries) {
        if (entry.hash == hash)
            return entry.id;
    }
    return -1;
}

} // namespace

TEST_CASE("Store::open 建立 v4 schema", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    const QString dbPath = temp.path() + QStringLiteral("/history.db");
    Store store;
    QString error;
    REQUIRE(store.open(dbPath, &error));
    REQUIRE(userVersion(dbPath) == 4);
    REQUIRE(store.count() == 0);

    const QVector<Folder> folders = store.folders();
    REQUIRE(folders.size() == 1);
    REQUIRE(folders.first().id == Store::kDefaultFolderId);
    REQUIRE(folders.first().name == QStringLiteral("收藏"));
    REQUIRE(folders.first().isDefault);
}

TEST_CASE("Store::open 把 v1 库迁移到 v4", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    const QString dbPath = temp.path() + QStringLiteral("/history.db");
    createV1Database(dbPath);

    Store store;
    QString error;
    REQUIRE(store.open(dbPath, &error));
    REQUIRE(userVersion(dbPath) == 4);
    REQUIRE(store.count() == 1);

    const QVector<Entry> entries = store.recent();
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.first().text == QStringLiteral("legacy text"));
    REQUIRE(entries.first().kind == ClipKind::Text);

    REQUIRE(store.folders().size() == 1);
    REQUIRE(store.favoriteMap().isEmpty());
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

    const qint64 id = entry.id;
    REQUIRE(store.setMembership(id, Store::kDefaultFolderId, true));
    REQUIRE(store.addItem(textItem(QStringLiteral("h1"), QStringLiteral("hello"), 3000)));
    REQUIRE(entryIdOf(store, QStringLiteral("h1")) == id);
    REQUIRE(store.isFavorite(id));
}

TEST_CASE("Store::touchEntry 把复用项刷新为最新", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));

    REQUIRE(store.addItem(textItem(QStringLiteral("old"), QStringLiteral("old text"), 1000)));
    REQUIRE(store.addItem(textItem(QStringLiteral("new"), QStringLiteral("new text"), 2000)));

    const QVector<Entry> before = store.recent();
    REQUIRE(before.size() == 2);
    const qint64 oldId = before.last().id;

    const qint64 startedAt = QDateTime::currentMSecsSinceEpoch();
    REQUIRE(store.touchEntry(oldId));
    const qint64 endedAt = QDateTime::currentMSecsSinceEpoch();

    Entry entry;
    REQUIRE(store.entry(oldId, &entry));
    REQUIRE(entry.createdAt >= startedAt);
    REQUIRE(entry.createdAt <= endedAt);
    REQUIRE(store.recent().first().id == oldId);
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

TEST_CASE("Store::open 拒绝高于当前 schema 的数据库", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    const QString dbPath = temp.path() + QStringLiteral("/history.db");
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("future_setup"));
        db.setDatabaseName(dbPath);
        REQUIRE(db.open());

        QSqlQuery query(db);
        REQUIRE(query.exec(QStringLiteral("CREATE TABLE entries (id INTEGER PRIMARY KEY)")));
        REQUIRE(query.exec(QStringLiteral("PRAGMA user_version = 99")));
        db.close();
    }
    QSqlDatabase::removeDatabase(QStringLiteral("future_setup"));

    Store store;
    QString error;
    REQUIRE_FALSE(store.open(dbPath, &error));
    REQUIRE(error.contains(QStringLiteral("高于")));
    REQUIRE(userVersion(dbPath) == 99);
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

TEST_CASE("Store::setMaxEntries 立即裁剪到新上限", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));

    for (int i = 0; i < 105; ++i) {
        const QString hash = QStringLiteral("h%1").arg(i);
        REQUIRE(store.addItem(textItem(hash, QStringLiteral("text %1").arg(i), 1000 + i)));
    }
    REQUIRE(store.count() == 105);

    store.setMaxEntries(100);
    REQUIRE(store.maxEntries() == 100);
    REQUIRE(store.count() == 100);
    REQUIRE(store.recent(1).first().hash == QStringLiteral("h104"));

    const QVector<Entry> entries = store.recent(100);
    for (const Entry &entry : entries)
        REQUIRE(entry.hash != QStringLiteral("h0"));
}

TEST_CASE("Store::setMaxEntries 夹取到允许范围", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));

    store.setMaxEntries(1);
    REQUIRE(store.maxEntries() == Store::kMinRetention);

    store.setMaxEntries(999999);
    REQUIRE(store.maxEntries() == Store::kMaxRetention);
}

TEST_CASE("Store 自定义收藏夹可创建、重命名与删除", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));

    qint64 id = 0;
    REQUIRE(store.addFolder(QStringLiteral("  工作  "), &id));
    REQUIRE(id > 0);
    REQUIRE(store.folders().size() == 2);
    REQUIRE(store.folders().last().name == QStringLiteral("工作"));

    REQUIRE(store.renameFolder(id, QStringLiteral("常用")));
    REQUIRE(store.folders().last().name == QStringLiteral("常用"));

    REQUIRE_FALSE(store.addFolder(QStringLiteral("   ")));
    REQUIRE_FALSE(store.removeFolder(Store::kDefaultFolderId));
    REQUIRE(store.removeFolder(id));
    REQUIRE(store.folders().size() == 1);
}

TEST_CASE("Store 收藏关系与按收藏夹检索", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));
    REQUIRE(store.addItem(textItem(QStringLiteral("h1"), QStringLiteral("alpha text"), 1000)));
    REQUIRE(store.addItem(textItem(QStringLiteral("h2"), QStringLiteral("beta text"), 2000)));

    const qint64 alphaId = entryIdOf(store, QStringLiteral("h1"));
    const qint64 betaId = entryIdOf(store, QStringLiteral("h2"));
    REQUIRE(alphaId > 0);
    REQUIRE(betaId > 0);

    qint64 workId = 0;
    REQUIRE(store.addFolder(QStringLiteral("工作"), &workId));

    REQUIRE_FALSE(store.isFavorite(alphaId));
    REQUIRE(store.setMembership(alphaId, Store::kDefaultFolderId, true));
    REQUIRE(store.setMembership(alphaId, workId, true));
    REQUIRE(store.isFavorite(alphaId));

    const QHash<qint64, QVector<qint64>> before = store.favoriteMap();
    REQUIRE(before.value(alphaId).size() == 2);
    REQUIRE_FALSE(before.contains(betaId));

    REQUIRE(store.recent(200, Store::kDefaultFolderId).size() == 1);
    REQUIRE(store.recent(200, workId).first().id == alphaId);
    REQUIRE(store.search(QStringLiteral("alpha"), workId).size() == 1);
    REQUIRE(store.search(QStringLiteral("beta"), workId).isEmpty());
    REQUIRE(store.search(QString()).size() == 2);
    REQUIRE(store.search(QString(), workId).size() == 1);

    REQUIRE(store.setMembership(alphaId, workId, false));
    const QHash<qint64, QVector<qint64>> after = store.favoriteMap();
    REQUIRE(after.value(alphaId).size() == 1);
    REQUIRE(store.recent(200, workId).isEmpty());
    REQUIRE(store.isFavorite(alphaId));
}

TEST_CASE("Store 删除历史或收藏夹时级联清理收藏关系", "[store]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    Store store;
    REQUIRE(store.open(temp.path() + QStringLiteral("/history.db")));
    REQUIRE(store.addItem(textItem(QStringLiteral("h1"), QStringLiteral("alpha"), 1000)));
    REQUIRE(store.addItem(textItem(QStringLiteral("h2"), QStringLiteral("beta"), 2000)));

    const qint64 alphaId = entryIdOf(store, QStringLiteral("h1"));
    const qint64 betaId = entryIdOf(store, QStringLiteral("h2"));
    REQUIRE(alphaId > 0);
    REQUIRE(betaId > 0);

    qint64 workId = 0;
    REQUIRE(store.addFolder(QStringLiteral("工作"), &workId));
    REQUIRE(store.setMembership(alphaId, Store::kDefaultFolderId, true));
    REQUIRE(store.setMembership(alphaId, workId, true));
    REQUIRE(store.setMembership(betaId, Store::kDefaultFolderId, true));

    REQUIRE(store.removeFolder(workId));
    REQUIRE(store.favoriteMap().value(alphaId).size() == 1);

    REQUIRE(store.removeEntry(alphaId));
    REQUIRE_FALSE(store.favoriteMap().contains(alphaId));
    REQUIRE(store.favoriteMap().value(betaId).size() == 1);

    REQUIRE(store.clearAll());
    REQUIRE(store.favoriteMap().isEmpty());
}
