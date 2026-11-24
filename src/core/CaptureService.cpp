#include "core/CaptureService.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFutureWatcher>
#include <QImage>
#include <QtConcurrent>

namespace {

constexpr int kThumbnailSide = 128;

QByteArray hashBytes(const char *kind, const QByteArray &payload)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray prefix(kind);
    prefix.append('\0');
    hash.addData(prefix);
    hash.addData(payload);
    return hash.result().toHex();
}

QByteArray encodePng(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

PreparedItem process(const ClipboardItem &item, const BlobStore &blobs, qint64 createdAt)
{
    PreparedItem prepared;
    prepared.kind = item.kind;
    prepared.createdAt = createdAt;

    switch (item.kind) {
    case ClipKind::Text: {
        if (item.text.trimmed().isEmpty())
            return prepared;
        const QByteArray utf8 = item.text.toUtf8();
        prepared.hash = QString::fromLatin1(hashBytes("text", utf8));
        prepared.text = item.text;
        prepared.byteSize = utf8.size();
        prepared.valid = true;
        return prepared;
    }
    case ClipKind::Files: {
        QStringList paths;
        for (const QString &path : item.paths) {
            if (!path.isEmpty())
                paths.append(path);
        }
        if (paths.isEmpty())
            return prepared;
        prepared.text = paths.join(QLatin1Char('\n'));
        const QByteArray utf8 = prepared.text.toUtf8();
        prepared.hash = QString::fromLatin1(hashBytes("files", utf8));
        prepared.byteSize = utf8.size();
        prepared.valid = true;
        return prepared;
    }
    case ClipKind::Image: {
        QImage image = QImage::fromData(item.data);
        if (image.isNull())
            return prepared;

        const QByteArray png = encodePng(image);
        if (png.isEmpty())
            return prepared;

        prepared.hash = QString::fromLatin1(hashBytes("image", png));
        prepared.mime = QStringLiteral("image/png");
        prepared.width = image.width();
        prepared.height = image.height();
        prepared.byteSize = png.size();
        prepared.path = blobs.saveImage(prepared.hash, png);

        const QImage thumbnail = image.scaled(kThumbnailSide, kThumbnailSide, Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);
        prepared.thumbPath = blobs.saveThumbnail(prepared.hash, encodePng(thumbnail));

        prepared.valid = !prepared.path.isEmpty();
        return prepared;
    }
    }

    return prepared;
}

} // namespace

CaptureService::CaptureService(BlobStore &blobs, QObject *parent)
    : QObject(parent)
    , m_blobs(blobs)
{
}

void CaptureService::submit(const ClipboardItem &item)
{
    const qint64 createdAt = QDateTime::currentMSecsSinceEpoch();
    // 按值拷贝：工作线程只依赖目录字符串，不持有 Store 内部对象的生命周期。
    const BlobStore blobs = m_blobs;

    auto *watcher = new QFutureWatcher<PreparedItem>(this);
    connect(watcher, &QFutureWatcher<PreparedItem>::finished, this, [this, watcher] {
        const PreparedItem result = watcher->result();
        watcher->deleteLater();
        if (result.valid)
            emit ready(result);
    });
    watcher->setFuture(
        QtConcurrent::run([item, blobs, createdAt] { return process(item, blobs, createdAt); }));
}
