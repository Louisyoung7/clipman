#pragma once

#include <QString>

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
    void suppressNext() override;

private:
    void onChanged();
    void armSuppression();

    QClipboard *m_clipboard = nullptr;
    QString m_lastText;
    bool m_suppressArmed = false;
};
