#include "core/Store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {

constexpr int kMaxEntries = 1000;
constexpr int kSchemaVersion = 1;

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

    const QStringList statements = {
        QStringLiteral("CREATE TABLE IF NOT EXISTS entries ("
                       "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                       "  hash TEXT NOT NULL UNIQUE,"
                       "  text_content TEXT,"
                       "  byte_size INTEGER NOT NULL DEFAULT 0,"
                       "  created_at INTEGER NOT NULL"
                       ")"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_entries_created_at "
                       "ON entries(created_at DESC)"),
        QStringLiteral("PRAGMA user_version = 1"),
    };

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

bool Store::addText(const QString &text, QString *error)
{
    if (text.trimmed().isEmpty())
        return true;

    const QByteArray utf8 = text.toUtf8();
    const QByteArray hash = QCryptographicHash::hash(utf8, QCryptographicHash::Sha256).toHex();

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO entries(hash, text_content, byte_size, created_at) VALUES(?, ?, ?, ?) "
        "ON CONFLICT(hash) DO UPDATE SET "
        "  created_at = excluded.created_at, byte_size = excluded.byte_size"));
    query.addBindValue(QString::fromLatin1(hash));
    query.addBindValue(text);
    query.addBindValue(utf8.size());
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!query.exec()) {
        setError(error, query.lastError().text());
        return false;
    }

    prune();
    return true;
}

void Store::prune()
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "DELETE FROM entries WHERE id NOT IN "
        "(SELECT id FROM entries ORDER BY created_at DESC LIMIT ?)"));
    query.addBindValue(kMaxEntries);
    query.exec();
}

QVector<Entry> Store::recent(int limit) const
{
    QVector<Entry> entries;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, hash, text_content, created_at FROM entries "
        "ORDER BY created_at DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec())
        return entries;

    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toLongLong();
        entry.hash = query.value(1).toString();
        entry.text = query.value(2).toString();
        entry.createdAt = query.value(3).toLongLong();
        entries.append(entry);
    }
    return entries;
}

bool Store::clearAll(QString *error)
{
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
