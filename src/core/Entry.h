#pragma once

#include <QString>

struct Entry
{
    qint64 id = 0;
    QString hash;
    QString text;
    qint64 createdAt = 0; // msecs since epoch
};
