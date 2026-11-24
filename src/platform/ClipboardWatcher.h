#pragma once

#include "platform/ClipboardSource.h"

class QClipboard;

// 兜底实现：依赖 QClipboard::dataChanged。
// 无法枚举完整 TARGETS，也不处理 INCR。
class ClipboardWatcher : public ClipboardSource
{
    Q_OBJECT

public:
    explicit ClipboardWatcher(QClipboard *clipboard, QObject *parent = nullptr);

    void start() override;
    void suppressNext(const QString &text) override;

private:
    void onChanged();

    QClipboard *m_clipboard = nullptr;
    QString m_lastText;
    QString m_suppressedText;
};
