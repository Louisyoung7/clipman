#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QString>
#include <QStringList>

// 剪贴板内容的三种形态。数据库里以字符串持久化。
enum class ClipKind { Text, Image, Files };

QString clipKindToString(ClipKind kind);
ClipKind clipKindFromString(const QString &value);

// 平台层捕获到的原始内容。
struct ClipboardItem
{
    ClipKind kind = ClipKind::Text;
    QString mime;          // Image: 采集到的 image/png 等；其余为空
    QString text;          // Text: 原文；Files: 路径以 '\n' 连接（供 FTS）
    QByteArray data;       // Image: 原始编码字节
    QStringList paths;     // Files: 本地绝对路径（保序）
    int width = 0;         // 平台层不填，由处理器回填
    int height = 0;
};

// CaptureService 处理后的结果：已计算哈希、图片已落盘，可直接入库。
struct PreparedItem
{
    ClipKind kind = ClipKind::Text;
    QString hash;
    QString mime;
    QString text;
    QString path;          // Image: blobs/<hash>.png
    QString thumbPath;     // Image: blobs/<hash>.thumb.png
    int width = 0;
    int height = 0;
    int byteSize = 0;
    qint64 createdAt = 0;
    bool valid = false;
};

Q_DECLARE_METATYPE(ClipboardItem)
Q_DECLARE_METATYPE(PreparedItem)
