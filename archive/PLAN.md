# Clipman 开发规划

> 本文是项目的实施路线图：记"接下来做什么、做到什么程度算完成"。技术选型的依据、模块必需性与系统依赖清单见附录 A。

## 1. 项目定位

Ubuntu 24.04 / X11 下常驻托盘的剪贴板历史工具：

- 后台监听 CLIPBOARD selection，把文本、图片、文件路径写入本地 SQLite
- 通过全局快捷键唤起弹出式面板，支持搜索、键盘选择、回车回填
- 全程本地存储，无网络依赖，无云同步

## 2. 范围

### MVP 内

| 能力 | 说明 |
| --- | --- |
| 文本捕获 | UTF8_STRING / text/plain 优选，INCR 分块 |
| 图片与文件 | 图片存磁盘，库里存路径；`text/uri-list` 存路径列表 |
| 历史检索 | SQLite FTS5 前缀/分词查询，不用 `LIKE %x%` |
| 面板交互 | 快捷键唤起、搜索、上下选择、回车复制、Esc/失焦隐藏 |
| 生命周期 | 单实例、去重、条数上限、暂停记录、清空 |

### 明确不做（MVP 阶段）

- QML/Quick 界面（Widgets 足够）
- 云同步、多设备（`Qt6::Network` 仅用于 GitHub 更新检查，不做数据同步）
- Flatpak 分发（沙箱会阻断 selection owner 接管）
- OCR、二维码、语法高亮、崩溃上报

## 3. 技术基线

| 项 | 选型 |
| --- | --- |
| 语言/构建 | C++17、CMake ≥ 3.21、`qt_standard_project_setup` |
| UI | `Qt6::Widgets` + `Qt6::Gui` |
| 存储 | `Qt6::Sql`(QSQLITE) + FTS5，WAL，`PRAGMA user_version` 迁移 |
| 平台层 | libxcb + xcb-xfixes（首选），QClipboard（兜底） |
| 并发 | `Qt6::Concurrent` 承担图片缩放与哈希 |
| 桌面集成 | `QSystemTrayIcon`、QLockFile/D-Bus 单实例、Global Shortcuts portal |

**分层铁律**：`ui/` → `core/` → SQLite；`platform/` → `core/`。`core/` 不依赖 `QWidget`，`platform/` 不直接碰数据库。

## 4. 当前状态

首个提交 `2b5864e` 完成最小闭环，**M1 交互可用、M2 多格式与健壮性、M3 工程化与分发、M4 配置化均已完成（0.5.0）**：

- ✅ XFixes 监听 + TARGETS 枚举 + INCR，QClipboard 兜底
- ✅ SQLite 存储：WAL、SHA-256 去重、1000 条裁剪
- ✅ M1：X11 全局快捷键 `Ctrl+Alt+V`（`grab_key` 挂在 root window）
- ✅ M1：弹出式面板，搜索框 + `↑/↓`、`Enter`、`Esc`、单击复制
- ✅ M1：FTS5 检索（schema v2），输入即搜
- ✅ M1：单条删除（`Delete` / 右键菜单）
- ✅ M1：修复 `suppressNext()` 与 `m_owner` 同步，回填不再重复入库
- ✅ M2：文本 / 图片 / 文件路径三类内容（schema v3）
- ✅ M2：图片落盘 `blobs/<hash>.png` + 缩略图，裁剪/删除/清空同步清理
- ✅ M2：`QtConcurrent` 异步哈希、图片解码与缩略图
- ✅ M2：D-Bus 单实例唤醒 + 托盘「暂停记录」
- ✅ 0.4：托盘「设置」面板 + 检查更新（GitHub Releases → 下载 deb → pkexec 安装）
- ✅ M3：`tests/`（Catch2 + QtTest，17 个用例）接入 CTest
- ✅ M3：`.clang-format` / `.clang-tidy`，`-Wall -Wextra -Wpedantic` 零警告构建
- ✅ M3：SVG 图标、`.desktop`、`/etc/xdg/autostart` 自启项、托盘「开机自启」开关
- ✅ M3：CPack 生成 deb（AppImage 按决策延后）
- ✅ M4：保留条数可配置（100–10000），修改后立即裁剪并持久化
- ✅ M4：全局快捷键可配置（`X11Hotkey::apply`），冲突时回滚 UI 与旧 grab

## 5. 里程碑

### M1 交互可用（P0）

目标：不用鼠标也能完成"唤起 → 搜索 → 回填"。**已完成。**

- [x] 全局快捷键唤起面板：X11 `grab_key` 注册 `Ctrl+Alt+V`（portal 延后到 M4）
- [x] 面板改造：顶部搜索框 + 列表，`↑/↓` 移动、`Enter` 复制、`Esc` 关闭、失焦自动隐藏
- [x] 单击即复制（原先只有双击/回车触发 `itemActivated`）
- [x] FTS5 虚拟表 + `Store::search()`，写入时同步索引，迁移升到 v2
- [x] 单条删除：`Delete` 键 + 右键菜单
- [x] 修复 `XcbClipboardWatcher::suppressNext()` 空实现，回填时同步 `m_owner`

验收：复制一段文本后按快捷键，面板立即出现且焦点在搜索框；输入关键词能命中；回车回填后历史不新增重复条目。

### M2 多格式与健壮性（P1）

目标：文本 / 图片 / 文件路径三类内容可用，单实例与暂停记录到位。**已完成。**

- [x] `Entry` 增加 `kind`(text/image/files)、`mime`、`path`、`width/height`，迁移 v3
- [x] 平台层按 `TARGETS` 抓取 `image/png`、`image/jpeg`、`image/bmp`、`image/tiff`
- [x] 图片落盘到 `<AppDataLocation>/blobs/<hash>.png`，库里只存路径；清理时同步删文件（含缩略图）
- [x] 用 `Qt6::Concurrent` 异步生成缩略图与哈希，避免阻塞 GUI
- [x] `text/uri-list` 支持：文件管理器复制后可在面板显示路径列表
- [x] D-Bus 单实例：第二个实例唤醒已运行窗口，而不是直接退出（无 session bus 回退 `QLockFile`）
- [x] 托盘增加"暂停记录"开关

验收：截图后回到面板能看到缩略图并粘贴到 GIMP；复制文件后能还原为路径列表；再次启动程序时已有窗口被唤起。

### M3 工程化与分发（P2）

**已完成。**

- [x] `tests/` + Catch2（`core/`）+ QtTest（`ui/`），`enable_testing()` 接入 CTest
- [x] `.clang-format` / `.clang-tidy`，构建零警告
- [x] 打包资源：`.desktop` 文件、应用图标、系统级 `/etc/xdg/autostart` 自启 + 托盘「开机自启」开关（写 `~/.config/autostart`）
- [x] Deb 包：CPack `DEB` 生成 `clipman_0.1.0_amd64.deb`（`dpkg-buildpackage` 依赖 debhelper，本机不可用；AppImage 按决策延后）

验收：`ctest` 全绿；安装后应用菜单可见、开机自启、卸载不残留数据目录外的文件。

### M4 配置化（P3）

目标：把当前写死的两个参数交给用户。复用已有设置对话框（[SettingsDialog.cpp](src/ui/SettingsDialog.cpp)），不新建窗口。**已完成。**

- [x] 「通用」组增加**保留条数**：`QSpinBox`（100–10000，默认 1000）。`Store` 用可设置的成员替换 `kMaxEntries` 常量，保存后立即裁剪一次
- [x] 「通用」组增加**全局快捷键**：键位捕获控件；`X11Hotkey` 改为接受 keysym 与修饰键参数，改键时先 ungrab 旧键再 grab 新键，注册失败则回滚 UI 并提示占用
- [x] 补充测试：`Store::setMaxEntries()` 的裁剪边界；键位字符串 → keysym/修饰键的解析（纯逻辑，可单测）

验收：改成 200 条后历史立即裁到 200，重启保持；快捷键改为 `Ctrl+Shift+Space` 后旧键失效、新键唤起；目标键位被占用时配置不被写坏。

**不在 M4 内（后续候选）**

- 忽略应用列表：需读 selection owner 的 `_NET_WM_PID`/`WM_CLASS`，且要记住条目来源（schema v4）；部分应用借用隐藏窗口，可靠性有限
- 文本转二维码：可选 `libqrencode` 依赖（apt 4.1.1 可用）
- i18n：`qt6-l10n-tools` + .ts；面向外部用户后再做
- ~~`KSyntaxHighlighting` 预览~~：为装饰性预览引入 KF6 依赖不划算，决定不做。如需美化，用等宽字体 + JSON/XML 格式化自实现

## 6. 已知问题与风险

| 项 | 影响 | 处理 |
| --- | --- | --- |
| 快捷键固定为 `Ctrl+Alt+V` | 无法按个人习惯调整 | 已解决（M4 可配置） |
| INCR 单块超时 2s，无整体进度反馈 | 超大内容可能被截断且静默丢弃 | 维持静默丢弃，不引入桌面通知（M2 结论） |
| 单实例已改 D-Bus，无 session bus 回退 QLockFile | 精简环境下二次启动仍无唤醒 | 可接受，记录在案 |
| `Qt6::Concurrent` 已用于图片哈希/缩放 | 大图处理已离开 GUI 线程 | 已解决 |
| 全局快捷键在 Wayland 受限 | Wayland 下 `grab_key` 无效 | 明确不支持 Wayland（见下条） |
| Wayland + GNOME 缺少 `wlr-data-control` | 历史管理器读不到其他应用的剪贴板，非"排期能解决"而是能力不可达 | 不作为目标平台；若将来支持，仅限 KDE/wlroots 并单独立项 |
| 图片以字节哈希去重 | 同一图片经不同编码（PNG/JPEG）会存成两条 | 已接受，M2 范围内不做感知哈希 |
| 更新检查依赖 GitHub Releases API | 未认证配额 60 次/小时，触发限流时无法检查 | 已缓解：403 时自动改用网页接口兜底，不消耗 API 配额 |
| deb 构建环境 | 本地手动打包依赖本机 Qt 6.8.3；正式发行需用 CI（Ubuntu 24.04 系统 Qt 6.4.2）产物 | GitHub Actions 已配置，发布用其 artifact |

## 7. 完成定义（DoD）

任何任务合入前需满足：

1. 构建通过且无新增警告
2. 新增逻辑有对应测试或被现有测试覆盖（M3 之后强制）
3. `core/`、`platform/` 的依赖方向未被破坏
4. 数据库结构变更带 `user_version` 迁移，且能在旧库上直接升级
5. 面向用户的行为变化同步更新本规划中的勾选项

## 附录 A：技术选型细节

### A.1 系统依赖（Ubuntu 24.04）

| 用途 | 包 |
| --- | --- |
| Qt6 基础模块 | `qt6-base-dev`（Widgets/Gui/Sql/Concurrent 都在其中） |
| SQLite 驱动 | `libqt6sql6-sqlite` |
| X11 原生层 | `libxcb1-dev libxcb-xfixes0-dev libxcb-icccm4-dev libxfixes-dev` |
| 国际化（后期） | `qt6-l10n-tools` |

> Qt6 已**没有** `QtX11Extras`，X11 能力全部并入 `QtGui`，不要再去装 `qt6-x11extras-dev`。

### A.2 Qt 模块必需性

| 模块 | 用途 | 判定 |
| --- | --- | --- |
| `Qt6::Widgets` | 托盘、列表、设置窗口 | 必需（比 QML 更契合工具类应用） |
| `Qt6::Gui` | `QClipboard`、`QMimeData`、`QImage` | 必需 |
| `Qt6::Sql` | SQLite 存储历史 | 必需 |
| `Qt6::Concurrent` | 图片缩放、哈希挪出 GUI 线程 | 必需（M2 起实际使用） |
| `Qt6::Svg` | 托盘与格式图标 | 推荐 |
| `Qt6::DBus` | 单实例、桌面通知、快捷键 | 推荐 |
| `Qt6::Network` | GitHub Releases 更新检查与 deb 下载 | 必需（0.4 起使用） |
| `Qt6::Qml` + `Quick` | 现代化动画面板 | 可选，MVP 不上 |

### A.3 X11 原生层职责

| 库 | 职责 |
| --- | --- |
| `xcb` | 连接 X server、原生事件循环集成（`QAbstractNativeEventFilter`） |
| `xcb-xfixes` | `XFixesSelectSelectionInput` 监听 selection 变化（比 `dataChanged` 可靠） |
| `xcb-icccm` | selection 相关原子与属性约定，避免手写常量 |
| `xfixes` / `x11` | 兜底兼容 |

**能力边界**：`QClipboard` 只能拿到 Qt 缓存的常见格式；完整枚举 `TARGETS`、处理 `INCR` 分块传输、接管 selection owner 身份，必须走 libxcb 原生协议。

### A.4 桌面集成机制

| 能力 | 机制 |
| --- | --- |
| 托盘 | `QSystemTrayIcon` + `QIcon::fromTheme`（带内置 fallback） |
| 单实例 | `QLockFile`，或 `QDBusConnection` 注册 `org.clipman.Clipman` |
| 全局快捷键 | 首选 Global Shortcuts portal（xdg-desktop-portal）；X11 下用 xcb grab 兜底 |
| 通知 | `org.freedesktop.Notifications` via D-Bus |
| 自启 | `~/.config/autostart/*.desktop` |
| 打包 | Deb + `dpkg-buildpackage` 首选；AppImage 次选；Flatpak 最后（沙箱会卡住剪贴板与 owner 接管） |

### A.5 存储细节

- 全文搜索必须用 FTS5 虚拟表，不要用 `LIKE %x%`
- 图片/文件存磁盘，库里只存路径，否则库体积与查询都会恶化
- 去重：内容哈希建唯一索引，可选 `BLAKE3` 或 `QCryptographicHash`(SHA-256)，当前实现用 SHA-256
- 迁移：手写 `PRAGMA user_version` 迁移器，不引第三方 migration 库
- 日志：用 `QLoggingCategory`，不引 spdlog

### A.6 依赖方向

```
ui/         Widgets  ──┐
                       ├──▶  core/  (Store, Entry, Hasher)  ──▶  SQLite
platform/   X11Backend ─┘        ▲
  (libxcb/XFIXES, owner 接管)     │
                         纯 C++，无 Qt GUI 依赖，可单测
```

换 Wayland backend 时只改 `platform/` 一层。

### A.7 可选增强依赖

| 方向 | 依赖 |
| --- | --- |
| 文本转二维码 | `libqrencode` |
| 图片 OCR 检索 | `libtesseract` |
| 语法高亮 | `KSyntaxHighlighting`（KDE 生态，apt 可装） |
| 国际化 | Qt Linguist + `qt6-l10n-tools` |

崩溃上报暂不需要。
