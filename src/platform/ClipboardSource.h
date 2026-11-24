#pragma once

#include <QObject>
#include <QString>

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
    virtual void suppressNext(const QString &text) = 0;

signals:
    void textCaptured(const QString &text);
};
