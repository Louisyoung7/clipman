#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>
#include <QVector>

#include <memory>

#include <xcb/xcb.h>

// 通过 X11 passive grab 注册全局快捷键 Ctrl+Alt+V。
// 仅 X11 可用；Wayland / portal 支持留到 M4。
class X11Hotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    static std::unique_ptr<X11Hotkey> create(QObject *parent = nullptr);
    ~X11Hotkey() override;

    bool isActive() const { return m_active; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void activated();

private:
    explicit X11Hotkey(xcb_connection_t *connection, QObject *parent);

    bool initialize();
    bool findKeycode();
    bool grabAll();

    xcb_connection_t *m_conn = nullptr;
    xcb_window_t m_root = XCB_NONE;
    xcb_keycode_t m_keycode = 0;
    QVector<quint16> m_modifiers;
    bool m_active = false;
    bool m_filterInstalled = false;
};
