#include "core/Store.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

constexpr int kSchemaVersion = 4;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

Store::~Store()
{
    if (!m_db.isValid())
        return;

    const QString name = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
}

bool Store::open(const QString &dbPath, QString *error)
{
    const QFileInfo info(dbPath);
    if (!QDir().mkpath(info.absolutePath())) {
        setError(error, QStringLiteral("无法创建数据目录: %1").arg(info.absolutePath()));
        return false;
    }

    m_blobs = BlobStore(info.absolutePath() + QStringLiteral("/blobs"));
    QString blobError;
    if (!m_blobs.initialize(&blobError))
        qWarning() << "图片存储不可用:" << blobError;

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"));
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        setError(error, m_db.lastError().text());
        return false;
    }

    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    // 级联删除依赖外键；该开关必须在迁移事务之外生效。
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));

    return migrate(error);
}

bool Store::migrate(QString *error)
{
    QSqlQuery versionQuery(m_db);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) {
        setError(error, versionQuery.lastError().text());
        return false;
    }
    const int version = versionQuery.value(0).toInt();
    versionQuery.finish();

    if (version > kSchemaVersion) {
        setError(error, QStringLiteral("数据版本 %1 高于当前程序支持的 %2，请升级 Clipman 后再打开")
                            .arg(version)
                            .arg(kSchemaVersion));
        return false;
    }

    if (version == kSchemaVersion)
        return true;

    if (!m_db.transaction()) {
        setError(error, m_db.lastError().text());
        return false;
    }

    const QStringList statements = [version] {
        QStringList sql;

        if (version < 1) {
            sql << QStringLiteral("CREATE TABLE IF NOT EXISTS entries ("
                                  "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                  "  hash TEXT NOT NULL UNIQUE,"
                                  "  text_content TEXT,"
                                  "  byte_size INTEGER NOT NULL DEFAULT 0,"
                                  "  created_at INTEGER NOT NULL"
                                  ")")
                << QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_created_at "
                                  "ON entries(created_at DESC)");
        }

        if (version < 2) {
            sql << QStringLiteral("CREATE VIRTUAL TABLE IF NOT EXISTS entries_fts USING fts5("
                                  "  text_content, content='entries', content_rowid='id',"
                                  "  tokenize='unicode61'"
                                  ")")
                << QStringLiteral("CREATE TRIGGER IF NOT EXISTS entries_ai AFTER INSERT ON entries "
                                  "BEGIN "
                                  "  INSERT INTO entries_fts(rowid, text_content) "
                                  "  VALUES (new.id, new.text_content); "
                                  "END")
                << QStringLiteral("CREATE TRIGGER IF NOT EXISTS entries_ad AFTER DELETE ON entries "
                                  "BEGIN "
                                  "  INSERT INTO entries_fts(entries_fts, rowid, text_content) "
                                  "  VALUES ('delete', old.id, old.text_content); "
                                  "END")
                << QStringLiteral("CREATE TRIGGER IF NOT EXISTS entries_au AFTER UPDATE ON entries "
                                  "BEGIN "
                                  "  INSERT INTO entries_fts(entries_fts, rowid, text_content) "
                                  "  VALUES ('delete', old.id, old.text_content); "
                                  "  INSERT INTO entries_fts(rowid, text_content) "
                                  "  VALUES (new.id, new.text_content); "
                                  "END")
                << QStringLiteral("INSERT INTO entries_fts(entries_fts) VALUES ('rebuild')");
        }

        if (version < 3) {
            // 旧行自动成为 kind='text'；FTS 外部内容表只索引 text_content，无需 rebuild。
            sql << QStringLiteral(
                       "ALTER TABLE entries ADD COLUMN kind TEXT NOT NULL DEFAULT 'text'")
                << QStringLiteral("ALTER TABLE entries ADD COLUMN mime TEXT")
                << QStringLiteral("ALTER TABLE entries ADD COLUMN path TEXT")
                << QStringLiteral("ALTER TABLE entries ADD COLUMN width INTEGER")
                << QStringLiteral("ALTER TABLE entries ADD COLUMN height INTEGER");
        }

        if (version < 4) {
            sql << QStringLiteral("CREATE TABLE IF NOT EXISTS folders ("
                                  "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                                  "  name TEXT NOT NULL UNIQUE,"
                                  "  is_default INTEGER NOT NULL DEFAULT 0,"
                                  "  created_at INTEGER NOT NULL"
                                  ")")
                << QStringLiteral("CREATE TABLE IF NOT EXISTS entry_folders ("
                                  "  entry_id INTEGER NOT NULL,"
                                  "  folder_id INTEGER NOT NULL,"
                                  "  created_at INTEGER NOT NULL,"
                                  "  PRIMARY KEY (entry_id, folder_id),"
                                  "  FOREIGN KEY (entry_id) REFERENCES entries(id) ON DELETE CASCADE,"
                                  "  FOREIGN KEY (folder_id) REFERENCES folders(id) ON DELETE CASCADE"
                                  ")")
                << QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entry_folders_folder "
                                  "ON entry_folders(folder_id, created_at DESC)")
                << QStringLiteral("INSERT OR IGNORE INTO folders(id, name, is_default, created_at) "
                                  "VALUES(1, '收藏', 1, %1)")
                       .arg(QDateTime::currentMSecsSinceEpoch());
        }

        sql << QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion);
        return sql;
    }();

    QSqlQuery ddl(m_db);
    for (const QString &statement : statements) {
        if (!ddl.exec(statement)) {
            setError(error, ddl.lastError().text());
            m_db.rollback();
            return false;
        }
    }

    if (!m_db.commit()) {
        setError(error, m_db.lastError().text());
        return false;
    }
    return true;
}

bool Store::addItem(const PreparedItem &item, QString *error)
{
    if (!item.valid || item.hash.isEmpty())
        return true;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO entries(hash, kind, mime, text_content, path, width, height, byte_size, "
        "created_at) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(hash) DO UPDATE SET "
        "  created_at = excluded.created_at, byte_size = excluded.byte_size, "
        "  mime = excluded.mime, path = excluded.path, "
        "  width = excluded.width, height = excluded.height"));
    query.addBindValue(item.hash);
    query.addBindValue(clipKindToString(item.kind));
    query.addBindValue(item.mime);
    query.addBindValue(item.text); // 图片为空字符串，避免 FTS 触发器处理 NULL
    query.addBindValue(item.kind == ClipKind::Image ? QVariant(item.path) : QVariant());
    query.addBindValue(item.width);
    query.addBindValue(item.height);
    query.addBindValue(item.byteSize);
    query.addBindValue(item.createdAt > 0 ? item.createdAt : QDateTime::currentMSecsSinceEpoch());

    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }

    prune();
    return true;
}

bool Store::touchEntry(qint64 id, QString *error)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE entries SET created_at = ? WHERE id = ?"));
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    query.addBindValue(id);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

void Store::setMaxEntries(int entries)
{
    m_maxEntries = qBound(kMinRetention, entries, kMaxRetention);
    prune();
}

void Store::prune()
{
    if (count() <= m_maxEntries)
        return;

    struct RemovedEntry
    {
        QString hash;
        ClipKind kind;
    };
    QVector<RemovedEntry> removed;

    QSqlQuery overflow(m_db);
    overflow.prepare(QStringLiteral("SELECT hash, kind FROM entries WHERE id NOT IN "
                                    "(SELECT id FROM entries ORDER BY created_at DESC LIMIT ?)"));
    overflow.addBindValue(m_maxEntries);
    if (overflow.exec()) {
        while (overflow.next()) {
            removed.append(
                {overflow.value(0).toString(), clipKindFromString(overflow.value(1).toString())});
        }
        overflow.finish();
    }

    // 先删记录：只有数据库删除成功后才删图片文件，避免出现「记录还在、图片已丢」。
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM entries WHERE id NOT IN "
                                 "(SELECT id FROM entries ORDER BY created_at DESC LIMIT ?)"));
    query.addBindValue(m_maxEntries);
    if (!query.exec()) {
        qWarning() << "裁剪历史失败，保留图片文件:" << query.lastError().text();
        return;
    }

    for (const RemovedEntry &entry : removed) {
        if (entry.kind == ClipKind::Image)
            m_blobs.remove(entry.hash);
    }
}

bool Store::entry(qint64 id, Entry *out) const
{
    if (!out)
        return false;

    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("SELECT id, hash, kind, mime, text_content, path, width, height, created_at "
                       "FROM entries WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next())
        return false;

    out->id = query.value(0).toLongLong();
    out->hash = query.value(1).toString();
    out->kind = clipKindFromString(query.value(2).toString());
    out->mime = query.value(3).toString();
    out->text = query.value(4).toString();
    out->path = query.value(5).toString();
    out->width = query.value(6).toInt();
    out->height = query.value(7).toInt();
    out->createdAt = query.value(8).toLongLong();
    return true;
}

QVector<Entry> Store::recent(int limit, qint64 folderId) const
{
    QVector<Entry> entries;

    QString sql = QStringLiteral("SELECT e.id, e.hash, e.kind, e.mime, e.text_content, e.path, "
                                 "e.width, e.height, e.created_at FROM entries e");
    if (folderId >= 0)
        sql += QStringLiteral(" JOIN entry_folders ef ON ef.entry_id = e.id "
                              "WHERE ef.folder_id = ?");
    sql += QStringLiteral(" ORDER BY e.created_at DESC LIMIT ?");

    QSqlQuery query(m_db);
    query.prepare(sql);
    if (folderId >= 0)
        query.addBindValue(folderId);
    query.addBindValue(limit);
    if (!query.exec())
        return entries;

    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.hash = query.value(1).toString();
        entry.kind = clipKindFromString(query.value(2).toString());
        entry.mime = query.value(3).toString();
        entry.text = query.value(4).toString();
        entry.path = query.value(5).toString();
        entry.width = query.value(6).toInt();
        entry.height = query.value(7).toInt();
        entry.createdAt = query.value(8).toLongLong();
        entries.append(entry);
    }
    return entries;
}

QVector<Entry> Store::search(const QString &query, qint64 folderId, int limit) const
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return recent(limit, folderId);

    QStringList parts;
    const QStringList tokens = trimmed.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        bool hasContent = false;
        for (const QChar ch : token) {
            if (ch.isLetterOrNumber()) {
                hasContent = true;
                break;
            }
        }
        if (!hasContent)
            continue;

        QString escaped = token;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        parts << QStringLiteral("\"%1\"*").arg(escaped);
    }

    if (parts.isEmpty())
        return recent(limit, folderId);

    QVector<Entry> entries;

    QString sql = QStringLiteral(
        "SELECT e.id, e.hash, e.kind, e.mime, e.text_content, e.path, e.width, e.height, "
        "e.created_at "
        "FROM entries_fts JOIN entries e ON e.id = entries_fts.rowid ");
    if (folderId >= 0)
        sql += QStringLiteral("JOIN entry_folders ef ON ef.entry_id = e.id ");
    sql += QStringLiteral("WHERE entries_fts MATCH ? ");
    if (folderId >= 0)
        sql += QStringLiteral("AND ef.folder_id = ? ");
    sql += QStringLiteral("ORDER BY rank LIMIT ?");

    QSqlQuery ftsQuery(m_db);
    ftsQuery.prepare(sql);
    ftsQuery.addBindValue(parts.join(QLatin1Char(' ')));
    if (folderId >= 0)
        ftsQuery.addBindValue(folderId);
    ftsQuery.addBindValue(limit);
    if (!ftsQuery.exec()) {
        qWarning() << "FTS 查询失败，回退到最近记录:" << ftsQuery.lastError().text();
        return recent(limit, folderId);
    }

    while (ftsQuery.next()) {
        Entry entry;
        entry.id = ftsQuery.value(0).toLongLong();
        entry.hash = ftsQuery.value(1).toString();
        entry.kind = clipKindFromString(ftsQuery.value(2).toString());
        entry.mime = ftsQuery.value(3).toString();
        entry.text = ftsQuery.value(4).toString();
        entry.path = ftsQuery.value(5).toString();
        entry.width = ftsQuery.value(6).toInt();
        entry.height = ftsQuery.value(7).toInt();
        entry.createdAt = ftsQuery.value(8).toLongLong();
        entries.append(entry);
    }
    return entries;
}

bool Store::removeEntry(qint64 id, QString *error)
{
    QString hash;
    ClipKind kind = ClipKind::Text;
    {
        QSqlQuery lookup(m_db);
        lookup.prepare(QStringLiteral("SELECT hash, kind FROM entries WHERE id = ?"));
        lookup.addBindValue(id);
        if (!lookup.exec()) {
            setError(error, lookup.lastError().text());
            return false;
        }
        if (lookup.next()) {
            hash = lookup.value(0).toString();
            kind = clipKindFromString(lookup.value(1).toString());
        }
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM entries WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }

    // 记录删除成功后再清理 blob，避免删除失败时图片先丢。
    if (kind == ClipKind::Image)
        m_blobs.remove(hash);
    return true;
}

bool Store::clearAll(QString *error)
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM entries"))) {
        setError(error, query.lastError().text());
        return false;
    }

    // 记录清空成功后再删文件；失败时保留图片，至少不会出现空记录。
    m_blobs.clear();
    return true;
}

int Store::count() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM entries")) || !query.next())
        return 0;
    return query.value(0).toInt();
}

QVector<Folder> Store::folders() const
{
    QVector<Folder> result;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT id, name, is_default, created_at FROM folders "
                                   "ORDER BY is_default DESC, created_at ASC")))
        return result;

    while (query.next()) {
        Folder folder;
        folder.id = query.value(0).toLongLong();
        folder.name = query.value(1).toString();
        folder.isDefault = query.value(2).toBool();
        folder.createdAt = query.value(3).toLongLong();
        result.append(folder);
    }
    return result;
}

bool Store::addFolder(const QString &name, qint64 *id, QString *error)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setError(error, QStringLiteral("收藏夹名称不能为空"));
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("INSERT INTO folders(name, is_default, created_at) VALUES(?, 0, ?)"));
    query.addBindValue(trimmed);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    if (id)
        *id = query.lastInsertId().toLongLong();
    return true;
}

bool Store::renameFolder(qint64 id, const QString &name, QString *error)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        setError(error, QStringLiteral("收藏夹名称不能为空"));
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE folders SET name = ? WHERE id = ?"));
    query.addBindValue(trimmed);
    query.addBindValue(id);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool Store::removeFolder(qint64 id, QString *error)
{
    QSqlQuery lookup(m_db);
    lookup.prepare(QStringLiteral("SELECT is_default FROM folders WHERE id = ?"));
    lookup.addBindValue(id);
    if (!lookup.exec()) {
        setError(error, lookup.lastError().text());
        return false;
    }
    if (lookup.next() && lookup.value(0).toBool()) {
        setError(error, QStringLiteral("默认收藏夹不能删除"));
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM folders WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool Store::setMembership(qint64 entryId, qint64 folderId, bool member, QString *error)
{
    QSqlQuery query(m_db);
    if (member) {
        query.prepare(QStringLiteral("INSERT OR IGNORE INTO entry_folders"
                                     "(entry_id, folder_id, created_at) VALUES(?, ?, ?)"));
        query.addBindValue(entryId);
        query.addBindValue(folderId);
        query.addBindValue(QDateTime::currentMSecsSinceEpoch());
    } else {
        query.prepare(QStringLiteral("DELETE FROM entry_folders WHERE entry_id = ? AND folder_id = ?"));
        query.addBindValue(entryId);
        query.addBindValue(folderId);
    }
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool Store::isFavorite(qint64 entryId) const
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT 1 FROM entry_folders WHERE entry_id = ? LIMIT 1"));
    query.addBindValue(entryId);
    return query.exec() && query.next();
}

QHash<qint64, QVector<qint64>> Store::favoriteMap() const
{
    QHash<qint64, QVector<qint64>> result;
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT entry_id, folder_id FROM entry_folders")))
        return result;

    while (query.next())
        result[query.value(0).toLongLong()].append(query.value(1).toLongLong());
    return result;
}
