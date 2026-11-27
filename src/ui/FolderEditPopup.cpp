#include "ui/FolderEditPopup.h"

#include <QApplication>
#include <QEvent>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

namespace {

constexpr int kAccepted = 1;
constexpr int kRejected = 0;
constexpr int kGap = 4;

} // namespace

FolderEditPopup::FolderEditPopup(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName(QStringLiteral("folderEditPopup"));
    setMinimumWidth(260);
    setStyleSheet(QStringLiteral(
        "#folderEditPopup {"
        "  background: #ffffff;"
        "  border: 1px solid #d3d8e0;"
        "  border-radius: 8px;"
        "}"
        "#folderTitle { font-weight: 600; color: #202124; }"
        "QLineEdit {"
        "  padding: 6px 8px;"
        "  border: 1px solid #d3d8e0;"
        "  border-radius: 5px;"
        "}"
        "QPushButton {"
        "  padding: 5px 12px;"
        "  border: 1px solid #d3d8e0;"
        "  border-radius: 5px;"
        "  background: #f7f8fa;"
        "}"
        "QPushButton#folderOk {"
        "  background: #1a73e8;"
        "  color: #ffffff;"
        "  border-color: #1a73e8;"
        "}"));

    m_titleLabel = new QLabel(this);
    m_titleLabel->setObjectName(QStringLiteral("folderTitle"));

    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(tr("输入收藏夹名称"));

    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *cancelButton = new QPushButton(tr("取消"), this);
    m_okButton = new QPushButton(tr("确定"), this);
    m_okButton->setObjectName(QStringLiteral("folderOk"));
    buttons->addWidget(cancelButton);
    buttons->addWidget(m_okButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->addWidget(m_titleLabel);
    layout->addWidget(m_edit);
    layout->addLayout(buttons);

    connect(m_okButton, &QPushButton::clicked, this, [this] { finish(kAccepted); });
    connect(cancelButton, &QPushButton::clicked, this, [this] { finish(kRejected); });

    qApp->installEventFilter(this);
}

FolderEditPopup::~FolderEditPopup()
{
    qApp->removeEventFilter(this);
}

QString FolderEditPopup::getText(QWidget *parent, const QString &title, const QString &initial,
                                 const QPoint &anchor, bool *ok)
{
    FolderEditPopup popup(parent);
    popup.m_titleLabel->setText(title);
    popup.m_edit->setText(initial);

    QEventLoop loop;
    QObject::connect(&popup, &FolderEditPopup::finished, &loop, &QEventLoop::quit);
    popup.positionBelow(anchor);
    popup.show();
    if (!popup.m_done)
        loop.exec();

    if (ok)
        *ok = popup.m_result == kAccepted;
    return popup.m_result == kAccepted ? popup.m_edit->text().trimmed() : QString();
}

bool FolderEditPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != qApp || !isVisible())
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (!frameGeometry().contains(mouseEvent->globalPosition().toPoint()))
            finish(kRejected);
    }
    return QWidget::eventFilter(watched, event);
}

void FolderEditPopup::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    finish(kRejected);
}

void FolderEditPopup::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        finish(kAccepted);
        break;
    case Qt::Key_Escape:
        finish(kRejected);
        break;
    default:
        QWidget::keyPressEvent(event);
        break;
    }
}

void FolderEditPopup::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_edit->setFocus();
    m_edit->selectAll();
}

void FolderEditPopup::finish(int result)
{
    if (m_done)
        return;
    m_done = true;
    m_result = result;
    hide();
    emit finished();
}

void FolderEditPopup::positionBelow(const QPoint &anchor)
{
    adjustSize();

    QPoint pos(anchor.x(), anchor.y() + kGap);
    const QScreen *screen = QGuiApplication::screenAt(anchor);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        pos.setX(qBound(available.left(), pos.x(), available.right() - width() + 1));
        pos.setY(qBound(available.top(), pos.y(), available.bottom() - height() + 1));
    }
    move(pos);
}
