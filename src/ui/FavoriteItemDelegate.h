#pragma once

#include <QModelIndex>
#include <QRect>
#include <QStyledItemDelegate>

class QAbstractItemModel;
class QAbstractItemView;
class QEvent;
class QPainter;
class QStyleOptionViewItem;

// 在历史项右上角绘制收藏星标，点击时发出切换信号。
class FavoriteItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FavoriteItemDelegate(QAbstractItemView *view, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

signals:
    void starToggled(qint64 entryId);

private:
    QAbstractItemView *m_view = nullptr;

    QRect starRect(const QStyleOptionViewItem &option, const QModelIndex &index) const;
    QRect hitRect(const QStyleOptionViewItem &option, const QModelIndex &index) const;
};
