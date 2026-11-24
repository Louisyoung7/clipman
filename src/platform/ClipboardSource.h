#pragma once

#include <QObject>
#include <QString>

#include "core/ClipboardItem.h"

// 剪贴板监听后端的共同接口。
// 两种实现：QClipboard（兜底）与 libxcb/XFixes（首选）。
class ClipboardSource : public QObject
{
    Q_OBJECT

public:
    explicit ClipboardSource(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~ClipboardSource() override = default;

    virtual void start() = 0;

    // 忽略自身回填引起的下一次捕获事件（内部带超时，避免误吞用户复制）。
    virtual void suppressNext() = 0;

signals:
    void itemCaptured(const ClipboardItem &item);
};
