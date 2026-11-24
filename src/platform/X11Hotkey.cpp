#include "platform/X11Hotkey.h"

#include <QApplication>
#include <QDebug>
#include <QtGui/qguiapplication_platform.h>

#include <xcb/xcb.h>

#include <cstdlib>

namespace {

constexpr quint16 kNumLockMask = 16; // 常见映射为 Mod2

quint16 xcbModifiersFor(Qt::KeyboardModifiers modifiers)
{
    quint16 mask = 0;
    if (modifiers.testFlag(Qt::ShiftModifier))
        mask |= XCB_MOD_MASK_SHIFT;
    if (modifiers.testFlag(Qt::ControlModifier))
        mask |= XCB_MOD_MASK_CONTROL;
    if (modifiers.testFlag(Qt::AltModifier))
        mask |= XCB_MOD_MASK_1;
    if (modifiers.testFlag(Qt::MetaModifier))
        mask |= XCB_MOD_MASK_4;
    return mask;
}

} // namespace

std::unique_ptr<X11Hotkey> X11Hotkey::create(QObject *parent)
{
    auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->connection())
        return nullptr;

    std::unique_ptr<X11Hotkey> hotkey(new X11Hotkey(x11->connection(), parent));
    if (!hotkey->initialize())
        return nullptr;
    return hotkey;
}

X11Hotkey::X11Hotkey(xcb_connection_t *connection, QObject *parent)
    : QObject(parent)
    , m_conn(connection)
{
}

X11Hotkey::~X11Hotkey()
{
    if (m_filterInstalled && qApp)
        qApp->removeNativeEventFilter(this);

    if (!m_conn || m_root == XCB_NONE || !m_active)
        return;

    ungrabAll();
}

bool X11Hotkey::initialize()
{
    const xcb_screen_iterator_t screen = xcb_setup_roots_iterator(xcb_get_setup(m_conn));
    if (!screen.data)
        return false;

    // passive key grab 只有挂在 root 上才是全局的；挂在自建窗口上不会触发。
    m_root = screen.data->root;

    qApp->installNativeEventFilter(this);
    m_filterInstalled = true;
    return true;
}

bool X11Hotkey::apply(const HotkeySpec &spec, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (!m_conn || m_root == XCB_NONE)
        return fail(QStringLiteral("X11 连接不可用，无法注册全局快捷键"));

    const quint32 keysym = x11KeysymForQtKey(spec.key);
    if (keysym == 0)
        return fail(QStringLiteral("按键「%1」不支持作为全局快捷键").arg(spec.toString()));

    const xcb_keycode_t keycode = findKeycode(keysym);
    if (keycode == 0)
        return fail(QStringLiteral("当前键盘布局中找不到按键「%1」").arg(spec.toString()));

    const quint16 baseModifiers = xcbModifiersFor(spec.modifiers);

    const xcb_keycode_t previousKeycode = m_keycode;
    const quint16 previousBase = m_baseModifiers;
    const HotkeySpec previousSpec = m_spec;
    const bool previousActive = m_active;

    ungrabAll();
    m_active = false;
    m_keycode = keycode;
    m_baseModifiers = baseModifiers;

    if (!grabAll(baseModifiers)) {
        // 新键全部被占用：尽力恢复旧键，避免用户失去已有快捷键。
        m_keycode = previousKeycode;
        m_baseModifiers = previousBase;
        m_spec = previousSpec;
        m_modifiers.clear();
        if (previousActive && previousKeycode != 0 && grabAll(previousBase))
            m_active = true;
        return fail(QStringLiteral("快捷键「%1」已被其他程序占用").arg(spec.toString()));
    }

    m_spec = spec;
    m_active = true;
    if (error)
        error->clear();
    return true;
}

void X11Hotkey::release()
{
    ungrabAll();
    m_active = false;
}

xcb_keycode_t X11Hotkey::findKeycode(quint32 keysym) const
{
    const xcb_setup_t *setup = xcb_get_setup(m_conn);
    const xcb_keycode_t minKeycode = setup->min_keycode;
    const int count = setup->max_keycode - minKeycode + 1;
    if (count <= 0)
        return 0;

    xcb_get_keyboard_mapping_reply_t *reply = xcb_get_keyboard_mapping_reply(
        m_conn, xcb_get_keyboard_mapping(m_conn, minKeycode, static_cast<uint16_t>(count)),
        nullptr);
    if (!reply)
        return 0;

    const int perKeycode = reply->keysyms_per_keycode;
    const auto *keysyms =
        reinterpret_cast<const xcb_keysym_t *>(xcb_get_keyboard_mapping_keysyms(reply));

    xcb_keycode_t found = 0;
    for (int i = 0; i < count && perKeycode > 0; ++i) {
        for (int j = 0; j < perKeycode; ++j) {
            if (keysyms[i * perKeycode + j] == keysym) {
                found = static_cast<xcb_keycode_t>(minKeycode + i);
                break;
            }
        }
        if (found != 0)
            break;
    }

    free(reply);
    return found;
}

bool X11Hotkey::grabAll(quint16 baseModifiers)
{
    m_modifiers = {
        baseModifiers,
        static_cast<quint16>(baseModifiers | XCB_MOD_MASK_LOCK),
        static_cast<quint16>(baseModifiers | kNumLockMask),
        static_cast<quint16>(baseModifiers | XCB_MOD_MASK_LOCK | kNumLockMask),
    };

    bool anyGrabbed = false;
    for (quint16 modifier : m_modifiers) {
        const xcb_void_cookie_t cookie = xcb_grab_key_checked(
            m_conn, 0, m_root, modifier, m_keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        if (xcb_generic_error_t *error = xcb_request_check(m_conn, cookie)) {
            free(error);
        } else {
            anyGrabbed = true;
        }
    }

    if (!anyGrabbed) {
        m_modifiers.clear();
        return false;
    }

    xcb_flush(m_conn);
    return true;
}

void X11Hotkey::ungrabAll()
{
    if (!m_conn || m_root == XCB_NONE || m_keycode == 0)
        return;

    for (quint16 modifier : m_modifiers)
        xcb_ungrab_key(m_conn, m_keycode, m_root, modifier);
    m_modifiers.clear();
    xcb_flush(m_conn);
}

bool X11Hotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);

    if (!m_active)
        return false;

    if (eventType != QByteArrayLiteral("xcb_generic_event_t") || !message)
        return false;

    auto *event = static_cast<xcb_generic_event_t *>(message);
    if ((event->response_type & ~0x80) != XCB_KEY_PRESS)
        return false;

    const auto *key = reinterpret_cast<xcb_key_press_event_t *>(event);
    if (key->detail != m_keycode)
        return false;

    // 忽略 CapsLock/NumLock 与鼠标按键位，其余修饰键必须精确匹配。
    constexpr quint16 kRelevantMask = 0x00ff & ~(XCB_MOD_MASK_LOCK | kNumLockMask);
    if ((key->state & kRelevantMask) != (m_baseModifiers & kRelevantMask))
        return false;

    emit activated();
    return true;
}
