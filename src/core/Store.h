#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVector>

#include "core/Entry.h"

class Store
{
public:
    Store() = default;
    ~Store();

    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    bool open(const QString &dbPath, QString *error = nullptr);
    bool addText(const QString &text, QString *error = nullptr);
    bool removeEntry(qint64 id, QString *error = nullptr);
    bool clearAll(QString *error = nullptr);

    QVector<Entry> recent(int limit = 200) const;
    QVector<Entry> search(const QString &query, int limit = 200) const;
    int count() const;

private:
    bool migrate(QString *error);
    void prune();

    QSqlDatabase m_db;
};
