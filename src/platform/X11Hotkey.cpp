#include "platform/X11Hotkey.h"

#include <QApplication>
#include <QDebug>
#include <QtGui/qguiapplication_platform.h>

#include <xcb/xcb.h>

#include <cstdlib>

namespace {

constexpr xcb_keysym_t kKeysymV = 0x76; // XK_v
constexpr quint16 kBaseModifiers = XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1;
constexpr quint16 kNumLockMask = 16; // 常见映射为 Mod2

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

    for (quint16 modifier : m_modifiers)
        xcb_ungrab_key(m_conn, m_keycode, m_root, modifier);
    xcb_flush(m_conn);
}

bool X11Hotkey::initialize()
{
    const xcb_screen_iterator_t screen = xcb_setup_roots_iterator(xcb_get_setup(m_conn));
    if (!screen.data)
        return false;

    // passive key grab 只有挂在 root 上才是全局的；挂在自建窗口上不会触发。
    m_root = screen.data->root;

    if (!findKeycode() || !grabAll())
        return false;

    qApp->installNativeEventFilter(this);
    m_filterInstalled = true;
    return true;
}

bool X11Hotkey::findKeycode()
{
    const xcb_setup_t *setup = xcb_get_setup(m_conn);
    const xcb_keycode_t minKeycode = setup->min_keycode;
    const int count = setup->max_keycode - minKeycode + 1;
    if (count <= 0)
        return false;

    xcb_get_keyboard_mapping_reply_t *reply = xcb_get_keyboard_mapping_reply(
        m_conn, xcb_get_keyboard_mapping(m_conn, minKeycode, static_cast<uint16_t>(count)), nullptr);
    if (!reply)
        return false;

    const int perKeycode = reply->keysyms_per_keycode;
    const auto *keysyms =
        reinterpret_cast<const xcb_keysym_t *>(xcb_get_keyboard_mapping_keysyms(reply));

    bool found = false;
    for (int i = 0; i < count && perKeycode > 0; ++i) {
        for (int j = 0; j < perKeycode; ++j) {
            if (keysyms[i * perKeycode + j] == kKeysymV) {
                m_keycode = static_cast<xcb_keycode_t>(minKeycode + i);
                found = true;
                break;
            }
        }
        if (found)
            break;
    }

    free(reply);
    return found;
}

bool X11Hotkey::grabAll()
{
    m_modifiers = {
        kBaseModifiers,
        static_cast<quint16>(kBaseModifiers | XCB_MOD_MASK_LOCK),
        static_cast<quint16>(kBaseModifiers | kNumLockMask),
        static_cast<quint16>(kBaseModifiers | XCB_MOD_MASK_LOCK | kNumLockMask),
    };

    bool anyGrabbed = false;
    for (quint16 modifier : m_modifiers) {
        const xcb_void_cookie_t cookie = xcb_grab_key_checked(
            m_conn, 0, m_root, modifier, m_keycode,
            XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        if (xcb_generic_error_t *error = xcb_request_check(m_conn, cookie)) {
            free(error);
        } else {
            anyGrabbed = true;
        }
    }

    if (!anyGrabbed) {
        qWarning() << "无法注册全局快捷键 Ctrl+Alt+V（可能已被其他程序占用）";
        return false;
    }

    m_active = true;
    xcb_flush(m_conn);
    return true;
}

bool X11Hotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);

    if (eventType != QByteArrayLiteral("xcb_generic_event_t") || !message)
        return false;

    auto *event = static_cast<xcb_generic_event_t *>(message);
    if ((event->response_type & ~0x80) != XCB_KEY_PRESS)
        return false;

    const auto *key = reinterpret_cast<xcb_key_press_event_t *>(event);
    if (key->detail != m_keycode)
        return false;

    const quint16 needed = XCB_MOD_MASK_CONTROL | XCB_MOD_MASK_1;
    if ((key->state & needed) != needed)
        return false;

    emit activated();
    return true;
}
