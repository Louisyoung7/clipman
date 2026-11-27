# Clipman

[![build](https://github.com/Louisyoung7/clipman/actions/workflows/build.yml/badge.svg)](https://github.com/Louisyoung7/clipman/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Ubuntu 24.04 / X11 下的托盘剪贴板历史工具。后台监听 CLIPBOARD selection，把文本、图片、文件路径写入本地 SQLite；按全局快捷键唤起面板，搜索后回车即可回填。

全程本地存储，无账号、无云同步。

## 功能

- **多格式捕获**：文本（UTF8_STRING / text/plain，支持 INCR 分块）、图片（PNG/JPEG/BMP/TIFF）、文件路径（`text/uri-list`）
- **历史检索**：SQLite FTS5 前缀与分词查询，输入即搜
- **收藏**：点击历史项右上角星标即可收藏或取消，收藏状态跨重启保留
- **面板交互**：全局快捷键唤起，`↑/↓` 选择，`Enter` 回填，`Delete` 删除，`Esc` 或失焦隐藏，单击即复制
- **去重与容量**：SHA-256 内容去重，默认保留 1000 条，可在 100–10000 之间调整
- **托盘常驻**：暂停记录、清空历史、设置、开机自启、退出
- **单实例**：重复启动时通过 D-Bus 唤醒已运行窗口
- **配置化**：全局快捷键、保留条数、网络代理均可设置
- **检查更新**：读取 GitHub Releases，下载 deb 前校验 SHA256，经 `pkexec` 安装

## 系统要求

| 项 | 要求 |
| --- | --- |
| 发行版 | Ubuntu 24.04（其他使用 Qt 6.4+ 的 X11 桌面理论上可用） |
| 显示服务 | X11。**不支持 Wayland** |
| Qt | 6.4 及以上 |
| 原生依赖 | libxcb、xcb-xfixes、xcb-icccm、libxfixes |

## 安装

从 [Releases](https://github.com/Louisyoung7/clipman/releases) 下载 deb 包与对应的 `checksums.sha256`，校验后安装：

```bash
sha256sum -c checksums.sha256
sudo apt install ./clipman_<版本>_amd64.deb
```

## 从源码构建

安装依赖：

```bash
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential cmake pkg-config \
  qt6-base-dev libqt6sql6-sqlite libqt6svg6 \
  libxcb1-dev libxcb-xfixes0-dev libxcb-icccm4-dev libxfixes-dev \
  catch2
```

配置并构建：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

运行：

```bash
./build/clipman
```

不需要测试时，可在配置阶段加 `-DBUILD_TESTING=OFF` 省去 Catch2 依赖。

## 测试

```bash
ctest --test-dir build --output-on-failure
```

`tests/` 使用 Catch2 覆盖 `core/` 与 `platform/` 的纯逻辑，QtTest 覆盖 `ui/` 交互。

## 打包

```bash
cd build
cpack -G DEB
```

产物为 `clipman_<版本>_amd64.deb`。版本号唯一来源是 [CMakeLists.txt](CMakeLists.txt) 中的 `CLIPMAN_VERSION`。

## 使用

| 操作 | 方式 |
| --- | --- |
| 唤起面板 | 默认 `Ctrl+Alt+V`，可在设置中修改 |
| 搜索 | 唤起后直接输入关键词 |
| 选择 / 回填 | `↑` `↓` 选择，`Enter` 复制，或单击条目 |
| 删除单条 | `Delete` 或右键菜单 |
| 收藏 / 取消 | 点击历史项右上角星星：点一下收藏，再点一下取消 |
| 关闭面板 | `Esc` 或点击面板外 |
| 暂停记录 | 托盘菜单 |
| 设置 | 托盘菜单 → 设置：快捷键、保留条数、代理、检查更新 |

首次启动后程序常驻托盘。deb 包会安装系统级自启项 `/etc/xdg/autostart/clipman.desktop`，也可在托盘菜单中开关用户级自启。

## 数据与配置

| 内容 | 路径 |
| --- | --- |
| 历史数据库 | `~/.local/share/clipman/history.db` |
| 图片与缩略图 | `~/.local/share/clipman/blobs/` |
| 配置 | `~/.config/clipman/clipman.conf` |
| 用户级自启项 | `~/.config/autostart/clipman.desktop` |

数据全部保存在本机，删除上述目录即可完全清除。

## 项目结构

```
src/core/       存储、Blob、捕获处理、更新检查（不依赖 Widgets）
src/platform/   XFixes 监听、全局快捷键、单实例、开机自启
src/ui/         历史面板、星星委托、设置对话框、托盘控制器
tests/          Catch2 + QtTest 单元测试
packaging/      .desktop 与自启模板
resources/      图标
```

依赖方向固定为 `ui/`、`platform/` → `core/`，`core/` 不依赖 `QWidget`，`platform/` 不直接操作数据库。

## 已知限制

- 仅支持 X11；Wayland 下既无法可靠读取其他应用的剪贴板，也无法注册全局快捷键
- INCR 单块传输超时 2 秒，超大内容可能被静默丢弃
- 图片按编码后的字节哈希去重，同一图片的不同编码会存为两条
- 更新检查使用未认证的 GitHub API，每小时 60 次配额用尽时自动改用网页接口兜底

## 持续集成

[build.yml](.github/workflows/build.yml) 在 `ubuntu-24.04` 上完成构建、单元测试、deb 打包与 SHA256 校验文件生成。推送 `v*` 标签时，标签名必须与 `CLIPMAN_VERSION` 一致，否则构建失败。

## 许可证

[MIT](LICENSE)
