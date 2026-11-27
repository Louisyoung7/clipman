#include "ui/FolderEditPopup.h"

#include <QEvent>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLoggingCategory>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

Q_LOGGING_CATEGORY(lcFolderEdit, "clipman.ui.folderedit", QtWarningMsg)

namespace {

constexpr int kAccepted = 1;
constexpr int kRejected = 0;
constexpr int kGap = 4;

} // namespace

FolderEditPopup::FolderEditPopup(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint)
{
    // Qt::Popup 会 grab 键盘，fcitx 等输入法收不到 Shift 热键，无法切换中文。
    // Qt::Tool 不 grab 键盘，且不会让宿主历史面板自动隐藏。
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
    m_edit->setAttribute(Qt::WA_InputMethodEnabled, true);
    m_edit->setPlaceholderText(tr("输入收藏夹名称"));
    m_edit->installEventFilter(this);

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
}

FolderEditPopup::~FolderEditPopup() = default;

bool FolderEditPopup::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_edit && event->type() == QEvent::InputMethod) {
        auto *inputEvent = static_cast<QInputMethodEvent *>(event);
        m_composing = !inputEvent->preeditString().isEmpty();
        qCDebug(lcFolderEdit) << "input method, preedit" << inputEvent->preeditString()
                              << "commit" << inputEvent->commitString()
                              << "composing" << m_composing;
    }
    return QWidget::eventFilter(watched, event);
}

QString FolderEditPopup::getText(const QString &title, const QString &initial, const QPoint &anchor,
                                 bool *ok)
{
    qCDebug(lcFolderEdit) << "getText enter" << title << "anchor" << anchor;
    // 不挂到历史面板下：Qt::Popup 在其他窗口激活时会隐藏，子面板会被连带隐藏。
    FolderEditPopup popup(nullptr);
    popup.m_titleLabel->setText(title);
    popup.m_edit->setText(initial);

    QEventLoop loop;
    QObject::connect(&popup, &FolderEditPopup::finished, &loop, &QEventLoop::quit);
    popup.positionBelow(anchor);
    popup.show();
    qCDebug(lcFolderEdit) << "shown, focusWidget" << popup.focusWidget()
                          << "focusWindow" << QGuiApplication::focusWindow();
    if (!popup.m_done)
        loop.exec();

    qCDebug(lcFolderEdit) << "done" << popup.m_result << "text" << popup.m_edit->text();
    if (ok)
        *ok = popup.m_result == kAccepted;
    return popup.m_result == kAccepted ? popup.m_edit->text().trimmed() : QString();
}

void FolderEditPopup::hideEvent(QHideEvent *event)
{
    qCDebug(lcFolderEdit) << "hide, done" << m_done;
    QWidget::hideEvent(event);
    finish(kRejected);
}

void FolderEditPopup::keyPressEvent(QKeyEvent *event)
{
    qCDebug(lcFolderEdit) << "key" << event->key() << "composing" << m_composing;
    // 输入法合成中文时，回车/esc 属于选字，不能提前确认或关闭面板。
    if (m_composing) {
        QWidget::keyPressEvent(event);
        return;
    }

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
    qCDebug(lcFolderEdit) << "show, focusWidget" << focusWidget()
                          << "activeWindow" << isActiveWindow();
}

void FolderEditPopup::finish(int result)
{
    qCDebug(lcFolderEdit) << "finish" << result << "done" << m_done;
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
    qCDebug(lcFolderEdit) << "position" << pos << "size" << size();
}
