#pragma once

#include <QObject>

#include "core/BlobStore.h"
#include "core/ClipboardItem.h"

// 在 QtConcurrent 工作线程里完成哈希、图片解码/缩放与落盘，GUI 线程只做入库。
class CaptureService : public QObject
{
    Q_OBJECT

public:
    explicit CaptureService(BlobStore &blobs, QObject *parent = nullptr);

    void submit(const ClipboardItem &item);

signals:
    void ready(const PreparedItem &item);

private:
    BlobStore &m_blobs;
};
