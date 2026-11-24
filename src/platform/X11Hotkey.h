#pragma once

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

#include <xcb/xcb.h>

#include "platform/HotkeySpec.h"

// 通过 X11 passive grab 注册全局快捷键，支持运行期换键。
// 仅 X11 可用；Wayland 下 grab_key 无效（见 PLAN.md 已知限制）。
class X11Hotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    static std::unique_ptr<X11Hotkey> create(QObject *parent = nullptr);
    ~X11Hotkey() override;

    // 先释放旧键再注册新键；新键被占用时尽力恢复旧键并返回 false。
    bool apply(const HotkeySpec &spec, QString *error = nullptr);
    // 临时释放 grab（例如打开设置对话框录制快捷键时），保留当前 spec。
    void release();
    bool isActive() const { return m_active; }
    HotkeySpec currentSpec() const { return m_spec; }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
    void activated();

private:
    explicit X11Hotkey(xcb_connection_t *connection, QObject *parent);

    bool initialize();
    xcb_keycode_t findKeycode(quint32 keysym) const; // 0 表示未找到
    bool grabAll(quint16 baseModifiers);
    void ungrabAll();

    xcb_connection_t *m_conn = nullptr;
    xcb_window_t m_root = XCB_NONE;
    xcb_keycode_t m_keycode = 0;
    quint16 m_baseModifiers = 0;
    QVector<quint16> m_modifiers;
    HotkeySpec m_spec;
    bool m_active = false;
    bool m_filterInstalled = false;
};
