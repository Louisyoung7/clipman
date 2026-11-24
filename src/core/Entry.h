#pragma once

#include <QString>

#include "core/ClipboardItem.h"

struct Entry
{
    qint64 id = 0;
    QString hash;
    ClipKind kind = ClipKind::Text;
    QString mime;
    QString text;
    QString path; // Image 的 blob 路径
    int width = 0;
    int height = 0;
    qint64 createdAt = 0; // msecs since epoch
};
