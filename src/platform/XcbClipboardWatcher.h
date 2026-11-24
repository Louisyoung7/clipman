#pragma once

#include <QByteArray>
#include <QAbstractNativeEventFilter>
#include <QString>

#include <memory>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#include "platform/ClipboardSource.h"

class QTimer;

// 首选实现：XFixes 订阅 selection 变化，libxcb 原生取数据。
// 支持完整 TARGETS 枚举、UTF8_STRING/text-plain 选择、INCR 分块传输。
class XcbClipboardWatcher : public ClipboardSource, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    static std::unique_ptr<XcbClipboardWatcher> create(QObject *parent = nullptr);
    ~XcbClipboardWatcher() override;

    void start() override;
    void suppressNext(const QString &text) override;

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    enum class State { Idle, WaitingTargets, WaitingText, ReceivingIncr };

    XcbClipboardWatcher(xcb_connection_t *connection, QObject *parent);

    bool initialize();
    xcb_atom_t internAtom(const char *name) const;
    void requestTargets();
    void requestText(xcb_atom_t target);
    void handleSelectionNotify(const xcb_selection_notify_event_t *event);
    void handlePropertyNotify(const xcb_property_notify_event_t *event);
    void handleXfixesEvent(const xcb_xfixes_selection_notify_event_t *event);
    void finishTransfer();
    void reset();

    // 读取后总是删除属性：INCR 靠“删除属性”放行下一块，非 INCR 也顺手清理。
    QByteArray readProperty(xcb_atom_t *type) const;

    xcb_connection_t *m_conn = nullptr;
    xcb_window_t m_window = XCB_NONE;

    xcb_atom_t m_clipboardAtom = XCB_NONE;
    xcb_atom_t m_targetsAtom = XCB_NONE;
    xcb_atom_t m_utf8Atom = XCB_NONE;
    xcb_atom_t m_textPlainAtom = XCB_NONE;
    xcb_atom_t m_textPlainUtf8Atom = XCB_NONE;
    xcb_atom_t m_incrAtom = XCB_NONE;
    xcb_atom_t m_propertyAtom = XCB_NONE;

    uint8_t m_xfixesEventBase = 0;
    xcb_window_t m_owner = XCB_NONE;
    xcb_timestamp_t m_ownerTime = XCB_CURRENT_TIME;

    State m_state = State::Idle;
    xcb_atom_t m_target = XCB_NONE;
    QByteArray m_buffer;
    QTimer *m_timeout = nullptr;
    QString m_lastText;
    bool m_started = false;
};
