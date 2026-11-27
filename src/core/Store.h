#pragma once

#include <QHash>
#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "core/BlobStore.h"
#include "core/ClipboardItem.h"
#include "core/Entry.h"

struct Folder
{
    qint64 id = 0;
    QString name;
    bool isDefault = false;
    qint64 createdAt = 0;
};

class Store
{
public:
    static constexpr int kMinRetention = 100;
    static constexpr int kMaxRetention = 10000;
    static constexpr int kDefaultRetention = 1000;

    Store() = default;
    ~Store();

    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    bool open(const QString &dbPath, QString *error = nullptr);
    bool addItem(const PreparedItem &item, QString *error = nullptr);
    bool touchEntry(qint64 id, QString *error = nullptr);
    bool removeEntry(qint64 id, QString *error = nullptr);
    bool clearAll(QString *error = nullptr);

    bool entry(qint64 id, Entry *out) const;
    // folderId < 0 表示全部历史。
    QVector<Entry> recent(int limit = 200, qint64 folderId = -1) const;
    QVector<Entry> search(const QString &query, qint64 folderId = -1, int limit = 200) const;
    int count() const;

    static constexpr qint64 kDefaultFolderId = 1;

    QVector<Folder> folders() const;
    bool addFolder(const QString &name, qint64 *id = nullptr, QString *error = nullptr);
    bool renameFolder(qint64 id, const QString &name, QString *error = nullptr);
    bool removeFolder(qint64 id, QString *error = nullptr);
    bool setMembership(qint64 entryId, qint64 folderId, bool member,
                       QString *error = nullptr);
    bool isFavorite(qint64 entryId) const;
    QHash<qint64, QVector<qint64>> favoriteMap() const;

    // 夹取到 [kMinRetention, kMaxRetention]，并立即裁剪一次。
    void setMaxEntries(int entries);
    int maxEntries() const { return m_maxEntries; }

    BlobStore &blobStore() { return m_blobs; }
    const BlobStore &blobStore() const { return m_blobs; }

private:
    bool migrate(QString *error);
    void prune();

    QSqlDatabase m_db;
    BlobStore m_blobs;
    int m_maxEntries = kDefaultRetention;
};
