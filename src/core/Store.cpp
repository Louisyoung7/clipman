#include "core/Store.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

constexpr int kMaxEntries = 1000;
constexpr int kSchemaVersion = 3;

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

    if (version >= kSchemaVersion)
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
            sql << QStringLiteral("ALTER TABLE entries ADD COLUMN kind TEXT NOT NULL DEFAULT 'text'")
                << QStringLiteral("ALTER TABLE entries ADD COLUMN mime TEXT")
                << QStringLiteral("ALTER TABLE entries ADD COLUMN path TEXT")
                << QStringLiteral("ALTER TABLE entries ADD COLUMN width INTEGER")
                << QStringLiteral("ALTER TABLE entries ADD COLUMN height INTEGER");
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

void Store::prune()
{
    QSqlQuery overflow(m_db);
    overflow.prepare(QStringLiteral(
        "SELECT id, hash, kind FROM entries WHERE id NOT IN "
        "(SELECT id FROM entries ORDER BY created_at DESC LIMIT ?)"));
    overflow.addBindValue(kMaxEntries);
    if (!overflow.exec())
        return;

    while (overflow.next()) {
        if (clipKindFromString(overflow.value(2).toString()) == ClipKind::Image)
            m_blobs.remove(overflow.value(1).toString());
    }
    overflow.finish();

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM entries WHERE id NOT IN "
        "(SELECT id FROM entries ORDER BY created_at DESC LIMIT ?)"));
    query.addBindValue(kMaxEntries);
    query.exec();
}

void Store::removeBlobFor(qint64 id)
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT hash, kind FROM entries WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec() || !query.next())
        return;

    const QString hash = query.value(0).toString();
    const ClipKind kind = clipKindFromString(query.value(1).toString());
    query.finish();

    if (kind == ClipKind::Image)
        m_blobs.remove(hash);
}

bool Store::entry(qint64 id, Entry *out) const
{
    if (!out)
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, hash, kind, mime, text_content, path, width, height, created_at "
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

QVector<Entry> Store::recent(int limit) const
{
    QVector<Entry> entries;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, hash, kind, mime, text_content, path, width, height, created_at "
        "FROM entries ORDER BY created_at DESC LIMIT ?"));
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

QVector<Entry> Store::search(const QString &query, int limit) const
{
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return recent(limit);

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
        return recent(limit);

    QVector<Entry> entries;

    QSqlQuery ftsQuery(m_db);
    ftsQuery.prepare(QStringLiteral(
        "SELECT e.id, e.hash, e.kind, e.mime, e.text_content, e.path, e.width, e.height, "
        "e.created_at "
        "FROM entries_fts JOIN entries e ON e.id = entries_fts.rowid "
        "WHERE entries_fts MATCH ? "
        "ORDER BY rank LIMIT ?"));
    ftsQuery.addBindValue(parts.join(QLatin1Char(' ')));
    ftsQuery.addBindValue(limit);
    if (!ftsQuery.exec()) {
        qWarning() << "FTS 查询失败，回退到最近记录:" << ftsQuery.lastError().text();
        return recent(limit);
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
    removeBlobFor(id);

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM entries WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

bool Store::clearAll(QString *error)
{
    m_blobs.clear();

    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM entries"))) {
        setError(error, query.lastError().text());
        return false;
    }
    return true;
}

int Store::count() const
{
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM entries")) || !query.next())
        return 0;
    return query.value(0).toInt();
}
