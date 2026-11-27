#pragma once

#include <QModelIndex>
#include <QRect>
#include <QStyledItemDelegate>

class QAbstractItemModel;
class QEvent;
class QPainter;
class QStyleOptionViewItem;

// 在历史项右上角绘制收藏星标，点击时发出切换信号。
class FavoriteItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FavoriteItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

signals:
    void starToggled(qint64 entryId);

private:
    static QRect starRect(const QRect &itemRect);
    static QRect hitRect(const QRect &itemRect);
};
