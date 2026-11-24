#include "platform/ClipboardWatcher.h"

#include <QClipboard>
#include <QMimeData>

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

void ClipboardWatcher::suppressNext(const QString &text)
{
    m_suppressedText = text;
}

void ClipboardWatcher::onChanged()
{
    if (!m_clipboard)
        return;

    const QMimeData *mime = m_clipboard->mimeData(QClipboard::Clipboard);
    if (!mime || !mime->hasText())
        return;

    const QString text = mime->text();
    if (text.isEmpty() || text == m_lastText)
        return;

    if (text == m_suppressedText) {
        m_suppressedText.clear();
        m_lastText = text;
        return;
    }

    m_lastText = text;
    emit textCaptured(text);
}
