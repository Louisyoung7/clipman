#include "core/ClipboardItem.h"

QString clipKindToString(ClipKind kind)
{
    switch (kind) {
    case ClipKind::Image:
        return QStringLiteral("image");
    case ClipKind::Files:
        return QStringLiteral("files");
    case ClipKind::Text:
        break;
    }
    return QStringLiteral("text");
}

ClipKind clipKindFromString(const QString &value)
{
    if (value == QLatin1String("image"))
        return ClipKind::Image;
    if (value == QLatin1String("files"))
        return ClipKind::Files;
    return ClipKind::Text;
}
