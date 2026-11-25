#pragma once

#include <QKeySequence>
#include <QString>
#include <Qt>

// 默认全局快捷键；设置中未配置时使用。
inline constexpr char kDefaultHotkeySequence[] = "Ctrl+Alt+V";
// 打开设置界面的默认全局快捷键；设置中未配置时使用。
inline constexpr char kDefaultSettingsHotkeySequence[] = "Ctrl+Alt+C";

// QSettings 键名；主程序与设置对话框共用，避免两处写法不一致。
inline constexpr char kHotkeySequenceKey[] = "hotkey/sequence";
inline constexpr char kSettingsHotkeySequenceKey[] = "settingsHotkey/sequence";

// 平台无关的快捷键描述：Qt::Key + 修饰键。
// 与 X11 keysym 的换算见 x11KeysymForQtKey()，不依赖 xcb 头，便于单测。
struct HotkeySpec
{
    int key = 0; // Qt::Key
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    static bool parse(const QString &text, HotkeySpec *out, QString *error = nullptr);
    QString toString() const;
};

// 0 表示该按键不支持作为全局快捷键。
quint32 x11KeysymForQtKey(int qtKey);
