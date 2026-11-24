#include "platform/HotkeySpec.h"

#include <QKeyCombination>

namespace {

constexpr quint32 kKeysymF1 = 0xffbe;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

} // namespace

bool HotkeySpec::parse(const QString &text, HotkeySpec *out, QString *error)
{
    if (!out)
        return false;

    const QKeySequence sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.count() != 1) {
        setError(error, QStringLiteral("请只按一组组合键，例如 Ctrl+Alt+V"));
        return false;
    }

    const QKeyCombination chord = sequence[0];
    const int key = chord.key();
    if (key == Qt::Key_unknown || key == 0) {
        setError(error, QStringLiteral("没有识别到有效的按键"));
        return false;
    }

    const Qt::KeyboardModifiers modifiers = chord.keyboardModifiers();
    if (!(modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        setError(error, QStringLiteral("快捷键必须包含 Ctrl、Alt 或 Super"));
        return false;
    }

    if (x11KeysymForQtKey(key) == 0) {
        setError(error,
                 QStringLiteral("这个按键不支持作为全局快捷键，请改用字母、数字或 F1–F12"));
        return false;
    }

    out->key = key;
    out->modifiers = modifiers;
    if (error)
        error->clear();
    return true;
}

QString HotkeySpec::toString() const
{
    return QKeySequence(QKeyCombination(modifiers, Qt::Key(key)))
        .toString(QKeySequence::PortableText);
}

quint32 x11KeysymForQtKey(int qtKey)
{
    // ASCII 可打印区：A-Z 对应小写 Latin-1 keysym，数字与标点与 ASCII 同值。
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        return static_cast<quint32>(qtKey - Qt::Key_A + 'a');
    if (qtKey >= 0x20 && qtKey <= 0x7e)
        return static_cast<quint32>(qtKey);

    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12)
        return kKeysymF1 + static_cast<quint32>(qtKey - Qt::Key_F1);

    switch (qtKey) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return 0xff0d;
    case Qt::Key_Tab:
        return 0xff09;
    case Qt::Key_Backspace:
        return 0xff08;
    case Qt::Key_Escape:
        return 0xff1b;
    case Qt::Key_Home:
        return 0xff50;
    case Qt::Key_Left:
        return 0xff51;
    case Qt::Key_Up:
        return 0xff52;
    case Qt::Key_Right:
        return 0xff53;
    case Qt::Key_Down:
        return 0xff54;
    case Qt::Key_PageUp:
        return 0xff55;
    case Qt::Key_PageDown:
        return 0xff56;
    case Qt::Key_End:
        return 0xff57;
    case Qt::Key_Insert:
        return 0xff63;
    case Qt::Key_Delete:
        return 0xffff;
    default:
        return 0;
    }
}
