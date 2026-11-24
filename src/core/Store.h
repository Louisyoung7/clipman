#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "core/BlobStore.h"
#include "core/ClipboardItem.h"
#include "core/Entry.h"

class Store
{
public:
    Store() = default;
    ~Store();

    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    bool open(const QString &dbPath, QString *error = nullptr);
    bool addItem(const PreparedItem &item, QString *error = nullptr);
    bool removeEntry(qint64 id, QString *error = nullptr);
    bool clearAll(QString *error = nullptr);

    bool entry(qint64 id, Entry *out) const;
    QVector<Entry> recent(int limit = 200) const;
    QVector<Entry> search(const QString &query, int limit = 200) const;
    int count() const;

    BlobStore &blobStore() { return m_blobs; }
    const BlobStore &blobStore() const { return m_blobs; }

private:
    bool migrate(QString *error);
    void prune();

    QSqlDatabase m_db;
    BlobStore m_blobs;
};
