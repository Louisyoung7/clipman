#include "platform/ClipboardWatcher.h"

#include <QBuffer>
#include <QClipboard>
#include <QImage>
#include <QMimeData>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kSuppressTimeoutMs = 1500;

} // namespace

ClipboardWatcher::ClipboardWatcher(QClipboard *clipboard, QObject *parent)
    : ClipboardSource(parent)
    , m_clipboard(clipboard)
{
}

void ClipboardWatcher::start()
{
    if (!m_clipboard)
        return;

    connect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardWatcher::onChanged,
            Qt::UniqueConnection);
}

void ClipboardWatcher::armSuppression()
{
    m_suppressArmed = true;
    QTimer::singleShot(kSuppressTimeoutMs, this, [this] { m_suppressArmed = false; });
}

void ClipboardWatcher::suppressNext()
{
    armSuppression();
}

void ClipboardWatcher::onChanged()
{
    if (!m_clipboard)
        return;

    if (m_suppressArmed) {
        m_suppressArmed = false;
        return;
    }

    const QMimeData *mime = m_clipboard->mimeData(QClipboard::Clipboard);
    if (!mime)
        return;

    if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            if (image.save(&buffer, "PNG") && !png.isEmpty()) {
                ClipboardItem item;
                item.kind = ClipKind::Image;
                item.mime = QStringLiteral("image/png");
                item.data = png;
                emit itemCaptured(item);
                return;
            }
        }
    }

    if (mime->hasUrls()) {
        QStringList paths;
        const QList<QUrl> urls = mime->urls();
        for (const QUrl &url : urls) {
            const QString local = url.toLocalFile();
            if (!local.isEmpty())
                paths.append(local);
        }
        if (!paths.isEmpty()) {
            ClipboardItem item;
            item.kind = ClipKind::Files;
            item.paths = paths;
            item.text = paths.join(QLatin1Char('\n'));
            emit itemCaptured(item);
            return;
        }
    }

    if (!mime->hasText())
        return;

    const QString text = mime->text();
    if (text.isEmpty() || text == m_lastText)
        return;

    m_lastText = text;

    ClipboardItem item;
    item.kind = ClipKind::Text;
    item.text = text;
    emit itemCaptured(item);
}
