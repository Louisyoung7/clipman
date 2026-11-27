#include "ui/FavoriteItemDelegate.h"

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionViewItem>

#include <cmath>

Q_LOGGING_CATEGORY(lcStar, "clipman.ui.star", QtWarningMsg)

namespace {

constexpr qreal kPi = 3.14159265358979323846;
constexpr int kStarSize = 16;
constexpr int kStarMargin = 8;
constexpr int kHitPadding = 12;

QPainterPath starPath(const QRectF &rect)
{
    QPainterPath path;
    const qreal centerX = rect.center().x();
    const qreal centerY = rect.center().y();
    const qreal outer = qMin(rect.width(), rect.height()) / 2.0;
    const qreal inner = outer * 0.45;

    for (int i = 0; i < 10; ++i) {
        const qreal radius = (i % 2 == 0) ? outer : inner;
        const qreal angle = -kPi / 2 + i * kPi / 5;
        const QPointF point(centerX + radius * std::cos(angle),
                            centerY + radius * std::sin(angle));
        if (i == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    path.closeSubpath();
    return path;
}

QColor starBackground(const QStyleOptionViewItem &option)
{
    if (option.state & QStyle::State_Selected)
        return QColor(QStringLiteral("#e8f0fe"));
    if (option.state & QStyle::State_MouseOver)
        return QColor(QStringLiteral("#eef1f6"));
    return QColor(QStringLiteral("#f7f8fa"));
}

} // namespace

FavoriteItemDelegate::FavoriteItemDelegate(QAbstractItemView *view, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_view(view)
{
}

QRect FavoriteItemDelegate::starRect(const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    // editorEvent 收到的 option.rect 宽度随内容变化，甚至超出可视区；
    // visualRect 给出条目在 viewport 中的实际几何，绘制与点击才一致。
    QRect item = option.rect;
    if (m_view && index.isValid())
        item = m_view->visualRect(index);

    int right = item.right();
    const QWidget *widget = m_view ? m_view : option.widget;
    if (widget)
        right = qMin(right, widget->width() - 1);
    return QRect(right - kStarMargin - kStarSize, item.top() + kStarMargin,
                 kStarSize, kStarSize);
}

QRect FavoriteItemDelegate::hitRect(const QStyleOptionViewItem &option,
                                    const QModelIndex &index) const
{
    return starRect(option, index).adjusted(-kHitPadding, -kHitPadding, kHitPadding, kHitPadding);
}

void FavoriteItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QStyledItemDelegate::paint(painter, option, index);

    const QRect star = starRect(option, index);
    if (!star.isValid() || star.right() < 0 || star.bottom() < 0 ||
        (m_view && star.top() > m_view->height()))
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    // 遮住可能延伸到右上角的文本，使星标始终清晰。
    painter->fillRect(star.adjusted(-4, -4, 4, 4), starBackground(option));

    const bool favorite = index.data(Qt::UserRole + 2).toBool();
    QPainterPath path = starPath(QRectF(star));
    if (favorite) {
        painter->setBrush(QColor(QStringLiteral("#f5a623")));
        painter->setPen(Qt::NoPen);
    } else {
        painter->setBrush(Qt::NoBrush);
        QPen pen(QColor(QStringLiteral("#9aa0a6")));
        pen.setWidthF(1.4);
        painter->setPen(pen);
    }
    painter->drawPath(path);
    painter->restore();
}

bool FavoriteItemDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                       const QStyleOptionViewItem &option,
                                       const QModelIndex &index)
{
    const QRect hit = hitRect(option, index);
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint pos = mouseEvent->position().toPoint();
        const bool inside = hit.contains(pos);
        qCDebug(lcStar) << "event" << event->type() << "pos" << pos
                        << "itemRect" << option.rect << "viewWidth"
                        << (m_view ? m_view->width() : (option.widget ? option.widget->width() : -1))
                        << "starHit" << hit << "inside" << inside
                        << "row" << index.row()
                        << "entryId" << index.data(Qt::UserRole + 1).toLongLong()
                        << "starOn" << index.data(Qt::UserRole + 2).toBool();
        if (!inside)
            return QStyledItemDelegate::editorEvent(event, model, option, index);
        if (event->type() == QEvent::MouseButtonRelease)
            emit starToggled(index.data(Qt::UserRole + 1).toLongLong());
        return true;
    }
    default:
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
}
