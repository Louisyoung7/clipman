#pragma once

#include <QModelIndex>
#include <QPoint>
#include <QRect>
#include <QStyledItemDelegate>
#include <QTimer>

class QAbstractItemModel;
class QEvent;
class QPainter;
class QStyleOptionViewItem;

// 在历史项右上角绘制收藏星标，并区分星星上的短按 / 长按操作。
class FavoriteItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FavoriteItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

    void cancelPending();

signals:
    void starToggled(qint64 entryId);
    void starLongPressed(qint64 entryId, const QPoint &globalPos);

private:
    static QRect starRect(const QRect &itemRect);
    static QRect hitRect(const QRect &itemRect);

    QTimer m_longPressTimer;
    qint64 m_pendingEntryId = -1;
    bool m_longFired = false;
};
