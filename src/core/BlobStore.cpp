#include "core/BlobStore.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <utility>

namespace {

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

BlobStore::BlobStore(QString dir)
    : m_dir(std::move(dir))
{
}

bool BlobStore::initialize(QString *error)
{
    if (m_dir.isEmpty()) {
        setError(error, QStringLiteral("blobs 目录为空"));
        return false;
    }
    if (!QDir().mkpath(m_dir)) {
        setError(error, QStringLiteral("无法创建 blobs 目录: %1").arg(m_dir));
        return false;
    }
    return true;
}

QString BlobStore::thumbnailPath(const QString &hash) const
{
    return m_dir + QLatin1Char('/') + hash + QStringLiteral(".thumb.png");
}

QString BlobStore::imagePath(const QString &hash) const
{
    return m_dir + QLatin1Char('/') + hash + QStringLiteral(".png");
}

QString BlobStore::saveImage(const QString &hash, const QByteArray &png, QString *error) const
{
    return writeFile(imagePath(hash), png, error);
}

QString BlobStore::saveThumbnail(const QString &hash, const QByteArray &png, QString *error) const
{
    return writeFile(thumbnailPath(hash), png, error);
}

QString BlobStore::writeFile(const QString &path, const QByteArray &bytes, QString *error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return {};
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        setError(error, file.errorString());
        return {};
    }
    return path;
}

void BlobStore::remove(const QString &hash) const
{
    if (hash.isEmpty() || m_dir.isEmpty())
        return;

    QDir dir(m_dir);
    const QStringList files = dir.entryList({hash + QStringLiteral(".*")}, QDir::Files);
    for (const QString &name : files)
        QFile::remove(dir.filePath(name));
}

void BlobStore::clear() const
{
    if (m_dir.isEmpty())
        return;

    QDir dir(m_dir);
    const QStringList files = dir.entryList(QDir::Files);
    for (const QString &name : files)
        QFile::remove(dir.filePath(name));
}
