#include "platform/XcbClipboardWatcher.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QTimer>
#include <QUrl>
#include <QtGui/qguiapplication_platform.h>

#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#include <cstdlib>
#include <cstring>

namespace {

constexpr int kTimeoutMs = 2000;
constexpr int kSuppressTimeoutMs = 1500;
constexpr int kMaxBytes = 32 * 1024 * 1024;
constexpr auto kClipboardName = "CLIPBOARD";
constexpr auto kPropertyName = "CLIPORY_SELECTION";

} // namespace

std::unique_ptr<XcbClipboardWatcher> XcbClipboardWatcher::create(QObject *parent)
{
    auto *x11 = qApp->nativeInterface<QNativeInterface::QX11Application>();
    if (!x11 || !x11->connection())
        return nullptr;

    std::unique_ptr<XcbClipboardWatcher> watcher(
        new XcbClipboardWatcher(x11->connection(), parent));
    if (!watcher->initialize())
        return nullptr;
    return watcher;
}

XcbClipboardWatcher::XcbClipboardWatcher(xcb_connection_t *connection, QObject *parent)
    : ClipboardSource(parent)
    , m_conn(connection)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &XcbClipboardWatcher::reset);

    m_suppressTimer = new QTimer(this);
    m_suppressTimer->setSingleShot(true);
    connect(m_suppressTimer, &QTimer::timeout, this, [this] { m_suppressArmed = false; });
}

XcbClipboardWatcher::~XcbClipboardWatcher()
{
    if (m_started && qApp)
        qApp->removeNativeEventFilter(this);

    if (m_conn && m_window != XCB_NONE) {
        xcb_destroy_window(m_conn, m_window);
        xcb_flush(m_conn);
    }
}

bool XcbClipboardWatcher::initialize()
{
    const xcb_query_extension_reply_t *extension = xcb_get_extension_data(m_conn, &xcb_xfixes_id);
    if (!extension || !extension->present)
        return false;
    m_xfixesEventBase = extension->first_event;

    free(xcb_xfixes_query_version_reply(m_conn, xcb_xfixes_query_version(m_conn, 5, 0), nullptr));

    m_clipboardAtom = internAtom(kClipboardName);
    m_targetsAtom = internAtom("TARGETS");
    m_utf8Atom = internAtom("UTF8_STRING");
    m_textPlainAtom = internAtom("text/plain");
    m_textPlainUtf8Atom = internAtom("text/plain;charset=utf-8");
    m_uriListAtom = internAtom("text/uri-list");
    m_pngAtom = internAtom("image/png");
    m_jpegAtom = internAtom("image/jpeg");
    m_bmpAtom = internAtom("image/bmp");
    m_tiffAtom = internAtom("image/tiff");
    m_incrAtom = internAtom("INCR");
    m_propertyAtom = internAtom(kPropertyName);

    if (m_clipboardAtom == XCB_NONE || m_targetsAtom == XCB_NONE || m_utf8Atom == XCB_NONE ||
        m_textPlainAtom == XCB_NONE || m_textPlainUtf8Atom == XCB_NONE ||
        m_uriListAtom == XCB_NONE || m_incrAtom == XCB_NONE || m_propertyAtom == XCB_NONE) {
        return false;
    }

    const xcb_screen_iterator_t screen = xcb_setup_roots_iterator(xcb_get_setup(m_conn));
    if (!screen.data)
        return false;

    m_window = xcb_generate_id(m_conn);
    const uint32_t eventMask = XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(m_conn, 0, m_window, screen.data->root, 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_ONLY, XCB_COPY_FROM_PARENT, XCB_CW_EVENT_MASK,
                      &eventMask);

    const uint32_t selectionMask = XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
                                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
                                   XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
    xcb_xfixes_select_selection_input(m_conn, m_window, m_clipboardAtom, selectionMask);
    xcb_flush(m_conn);
    return true;
}

void XcbClipboardWatcher::start()
{
    if (m_started)
        return;
    m_started = true;
    qApp->installNativeEventFilter(this);

    if (QGuiApplication::clipboard()->ownsClipboard())
        return;

    xcb_get_selection_owner_reply_t *reply = xcb_get_selection_owner_reply(
        m_conn, xcb_get_selection_owner(m_conn, m_clipboardAtom), nullptr);
    if (!reply)
        return;

    const xcb_window_t owner = reply->owner;
    free(reply);
    if (owner != XCB_NONE)
        requestTargets();
}

void XcbClipboardWatcher::armSuppression()
{
    m_suppressArmed = true;
    m_suppressTimer->start(kSuppressTimeoutMs);
}

void XcbClipboardWatcher::suppressNext()
{
    // 回填会以自身为 owner 触发一次监听；用标志忽略下一次取权事件。
    armSuppression();
}

bool XcbClipboardWatcher::nativeEventFilter(const QByteArray &eventType, void *message,
                                            qintptr *result)
{
    Q_UNUSED(result);

    if (eventType != QByteArrayLiteral("xcb_generic_event_t") || !message)
        return false;

    auto *event = static_cast<xcb_generic_event_t *>(message);
    const uint8_t response = event->response_type & ~0x80;

    if (m_xfixesEventBase != 0 && response == m_xfixesEventBase) {
        handleXfixesEvent(reinterpret_cast<const xcb_xfixes_selection_notify_event_t *>(event));
    } else if (response == XCB_SELECTION_NOTIFY) {
        handleSelectionNotify(reinterpret_cast<const xcb_selection_notify_event_t *>(event));
    } else if (response == XCB_PROPERTY_NOTIFY) {
        handlePropertyNotify(reinterpret_cast<const xcb_property_notify_event_t *>(event));
    }
    return false;
}

xcb_atom_t XcbClipboardWatcher::internAtom(const char *name) const
{
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
        m_conn, xcb_intern_atom(m_conn, 0, static_cast<uint16_t>(std::strlen(name)), name),
        nullptr);
    if (!reply)
        return XCB_NONE;

    const xcb_atom_t atom = reply->atom;
    free(reply);
    return atom;
}

void XcbClipboardWatcher::requestTargets()
{
    m_state = State::WaitingTargets;
    m_target = m_targetsAtom;
    m_targetMime.clear();
    m_buffer.clear();

    xcb_convert_selection(m_conn, m_window, m_clipboardAtom, m_targetsAtom, m_propertyAtom,
                          m_ownerTime);
    xcb_flush(m_conn);
    m_timeout->start(kTimeoutMs);
}

void XcbClipboardWatcher::requestTarget(xcb_atom_t target, const QString &mime)
{
    m_state = State::WaitingText;
    m_target = target;
    m_targetMime = mime;

    xcb_convert_selection(m_conn, m_window, m_clipboardAtom, target, m_propertyAtom, m_ownerTime);
    xcb_flush(m_conn);
    m_timeout->start(kTimeoutMs);
}

void XcbClipboardWatcher::handleXfixesEvent(const xcb_xfixes_selection_notify_event_t *event)
{
    if (event->selection != m_clipboardAtom)
        return;

    // SelectionWindowDestroy / SelectionClientClose 可能来自“上一任 owner”，
    // 晚于新 owner 的事件到达时不能打断正在进行的传输，交给超时兜底。
    if (event->subtype != XCB_XFIXES_SELECTION_EVENT_SET_SELECTION_OWNER)
        return;

    if (event->owner == XCB_NONE) {
        m_owner = XCB_NONE;
        reset();
        return;
    }

    // 同一个 owner 的同一次取权可能重复上报；传输中直接忽略，避免打断进行中的请求。
    if (event->owner == m_owner && (event->timestamp == m_ownerTime || m_state != State::Idle)) {
        return;
    }

    // 自身回填也会产生 SET_SELECTION_OWNER；先同步 owner 记账再决定是否抓取，
    // 避免 m_owner 停留在上一任 owner 导致后续判断失准。
    m_owner = event->owner;
    m_ownerTime = event->timestamp;

    if (m_suppressArmed) {
        m_suppressArmed = false;
        m_suppressTimer->stop();
        return;
    }

    if (QGuiApplication::clipboard()->ownsClipboard())
        return;

    requestTargets();
}

void XcbClipboardWatcher::handleSelectionNotify(const xcb_selection_notify_event_t *event)
{
    if (event->requestor != m_window || event->selection != m_clipboardAtom ||
        m_state == State::Idle) {
        return;
    }

    // 迟到的、属于上一阶段的 SelectionNotify（target 不匹配）不能当成当前阶段的结果。
    if (event->target != m_target)
        return;

    if (event->property == XCB_NONE) {
        reset();
        return;
    }

    xcb_atom_t type = XCB_NONE;
    const QByteArray data = readProperty(&type);

    if (m_state == State::WaitingTargets) {
        if (type != XCB_ATOM_ATOM || data.size() < static_cast<int>(sizeof(xcb_atom_t))) {
            reset();
            return;
        }

        const auto *atoms = reinterpret_cast<const xcb_atom_t *>(data.constData());
        const int count = data.size() / static_cast<int>(sizeof(xcb_atom_t));
        const auto hasTarget = [atoms, count](xcb_atom_t atom) {
            for (int i = 0; i < count; ++i) {
                if (atoms[i] == atom)
                    return true;
            }
            return false;
        };

        // 图片优先于 uri-list：截图工具常同时提供图片与临时文件路径，
        // 只有图片优先才能把截图回填到图像编辑器。
        xcb_atom_t chosen = XCB_NONE;
        QString mime;
        if (m_pngAtom != XCB_NONE && hasTarget(m_pngAtom)) {
            chosen = m_pngAtom;
            mime = QStringLiteral("image/png");
        } else if (m_jpegAtom != XCB_NONE && hasTarget(m_jpegAtom)) {
            chosen = m_jpegAtom;
            mime = QStringLiteral("image/jpeg");
        } else if (m_bmpAtom != XCB_NONE && hasTarget(m_bmpAtom)) {
            chosen = m_bmpAtom;
            mime = QStringLiteral("image/bmp");
        } else if (m_tiffAtom != XCB_NONE && hasTarget(m_tiffAtom)) {
            chosen = m_tiffAtom;
            mime = QStringLiteral("image/tiff");
        } else if (hasTarget(m_uriListAtom)) {
            chosen = m_uriListAtom;
            mime = QStringLiteral("text/uri-list");
        } else if (hasTarget(m_utf8Atom)) {
            chosen = m_utf8Atom;
        } else if (hasTarget(m_textPlainUtf8Atom)) {
            chosen = m_textPlainUtf8Atom;
        } else if (hasTarget(m_textPlainAtom)) {
            chosen = m_textPlainAtom;
        }

        if (chosen == XCB_NONE) {
            reset();
            return;
        }

        requestTarget(chosen, mime);
        return;
    }

    if (m_state != State::WaitingText)
        return;

    if (type == m_incrAtom) {
        // INCR：读取属性并删除即表示已就绪，随后按 PropertyNotify 逐块收取。
        m_buffer.clear();
        m_state = State::ReceivingIncr;
        m_timeout->start(kTimeoutMs);
        return;
    }

    m_buffer = data;
    finishTransfer();
}

void XcbClipboardWatcher::handlePropertyNotify(const xcb_property_notify_event_t *event)
{
    if (event->window != m_window || event->atom != m_propertyAtom)
        return;
    if (m_state != State::ReceivingIncr)
        return;

    // 只处理 NEW_VALUE：我们自己读取并删除属性同样会产生 DELETE 通知，
    // 把它当成结束会打断传输。结束以“零长度块”为准，异常情况交给超时。
    if (event->state != XCB_PROPERTY_NEW_VALUE)
        return;

    xcb_atom_t type = XCB_NONE;
    const QByteArray chunk = readProperty(&type);
    if (chunk.isEmpty()) {
        finishTransfer();
        return;
    }

    m_buffer += chunk;
    if (m_buffer.size() > kMaxBytes) {
        reset();
        return;
    }
    m_timeout->start(kTimeoutMs);
}

void XcbClipboardWatcher::emitCollected(const QByteArray &raw)
{
    QByteArray data = raw;
    const xcb_atom_t target = m_target;
    const QString mime = m_targetMime;

    while (data.endsWith('\0'))
        data.chop(1);

    if (data.isEmpty())
        return;

    if (target == m_pngAtom || target == m_jpegAtom || target == m_bmpAtom ||
        target == m_tiffAtom) {
        ClipboardItem item;
        item.kind = ClipKind::Image;
        item.mime = mime;
        item.data = data;
        emit itemCaptured(item);
        return;
    }

    if (target == m_uriListAtom) {
        QStringList paths;
        const QList<QByteArray> lines = data.split('\n');
        for (QByteArray line : lines) {
            if (line.endsWith('\r'))
                line.chop(1);
            line = line.trimmed();
            if (line.isEmpty() || line.startsWith('#'))
                continue;
            const QString local = QUrl::fromEncoded(line).toLocalFile();
            if (!local.isEmpty())
                paths.append(local);
        }
        if (paths.isEmpty())
            return;

        ClipboardItem item;
        item.kind = ClipKind::Files;
        item.paths = paths;
        item.text = paths.join(QLatin1Char('\n'));
        emit itemCaptured(item);
        return;
    }

    const QString text =
        (target == m_textPlainAtom) ? QString::fromLatin1(data) : QString::fromUtf8(data);
    if (text.trimmed().isEmpty() || text == m_lastText)
        return;

    m_lastText = text;

    ClipboardItem item;
    item.kind = ClipKind::Text;
    item.text = text;
    emit itemCaptured(item);
}

void XcbClipboardWatcher::finishTransfer()
{
    const QByteArray data = m_buffer;
    m_buffer.clear();

    if (data.size() > kMaxBytes) {
        reset();
        return;
    }

    emitCollected(data);
    reset();
}

void XcbClipboardWatcher::reset()
{
    m_state = State::Idle;
    m_target = XCB_NONE;
    m_targetMime.clear();
    m_buffer.clear();
    if (m_timeout)
        m_timeout->stop();
}

QByteArray XcbClipboardWatcher::readProperty(xcb_atom_t *type) const
{
    xcb_get_property_reply_t *reply =
        xcb_get_property_reply(m_conn,
                               xcb_get_property(m_conn, 1, m_window, m_propertyAtom,
                                                XCB_GET_PROPERTY_TYPE_ANY, 0, 0x400000),
                               nullptr);
    if (!reply)
        return {};

    if (type)
        *type = reply->type;

    QByteArray data;
    const int length = xcb_get_property_value_length(reply);
    if (length > 0)
        data = QByteArray(static_cast<const char *>(xcb_get_property_value(reply)), length);
    free(reply);
    return data;
}
