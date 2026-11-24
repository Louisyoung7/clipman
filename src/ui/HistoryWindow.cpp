#include "ui/HistoryWindow.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/Store.h"

HistoryWindow::HistoryWindow(Store &store, QWidget *parent)
    : QWidget(parent)
    , m_store(store)
{
    setWindowTitle(tr("剪贴板历史"));
    resize(560, 640);

    m_list = new QListWidget(this);
    m_list->setAlternatingRowColors(true);
    m_list->setTextElideMode(Qt::ElideRight);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_summary = new QLabel(this);
    auto *clearButton = new QPushButton(tr("清空"), this);

    auto *header = new QHBoxLayout;
    header->addWidget(m_summary);
    header->addStretch();
    header->addWidget(clearButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(header);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        if (item)
            emit entryActivated(item->data(Qt::UserRole).toString());
    });
    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_store.clearAll();
        refresh();
        emit cleared();
    });

    refresh();
}

void HistoryWindow::refresh()
{
    const QVector<Entry> entries = m_store.recent();

    m_list->clear();
    for (const Entry &entry : entries) {
        const QString stamp =
            QDateTime::fromMSecsSinceEpoch(entry.createdAt).toString(QStringLiteral("MM-dd HH:mm:ss"));
        auto *item = new QListWidgetItem(tr("%1  %2").arg(stamp, entry.text.simplified()));
        item->setData(Qt::UserRole, entry.text);
        item->setToolTip(entry.text.left(5000));
        m_list->addItem(item);
    }

    m_summary->setText(tr("共 %1 条（最多保留 1000 条）").arg(entries.size()));
}
