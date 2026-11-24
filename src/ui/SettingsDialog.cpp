#include "ui/SettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkProxyFactory>
#include <QNetworkProxyQuery>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "core/Store.h"
#include "platform/HotkeySpec.h"

namespace {

constexpr auto kRepoOwner = "Louisyoung7";
constexpr auto kRepoName = "clipman";

QString gsettingsGet(const QString &schema, const QString &key)
{
    QProcess process;
    process.start(QStringLiteral("gsettings"), {QStringLiteral("get"), schema, key});
    if (!process.waitForStarted(500) || !process.waitForFinished(1500))
        return {};
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};

    QString value = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('\'')) &&
        value.endsWith(QLatin1Char('\'')))
        value = value.mid(1, value.size() - 2);
    return value;
}

// Qt 6 在 Linux 上不再内置 libproxy，systemProxyForQuery() 只读环境变量，
// 所以「跟随系统」还需要补读 GNOME 桌面的代理设置。
QNetworkProxy gnomeProxy()
{
    if (gsettingsGet(QStringLiteral("org.gnome.system.proxy"), QStringLiteral("mode")) !=
        QLatin1String("manual"))
        return QNetworkProxy(QNetworkProxy::NoProxy);

    auto readProxy = [](const QString &schema) {
        const QString host = gsettingsGet(schema, QStringLiteral("host"));
        const int port = gsettingsGet(schema, QStringLiteral("port")).toInt();
        return QNetworkProxy(QNetworkProxy::HttpProxy, host,
                             static_cast<quint16>(qBound(0, port, 65535)));
    };

    QNetworkProxy proxy = readProxy(QStringLiteral("org.gnome.system.proxy.https"));
    if (proxy.hostName().isEmpty())
        proxy = readProxy(QStringLiteral("org.gnome.system.proxy.http"));
    if (proxy.hostName().isEmpty() || proxy.port() == 0)
        return QNetworkProxy(QNetworkProxy::NoProxy);
    return proxy;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent, HotkeyApplier hotkeyApplier)
    : QDialog(parent)
    , m_hotkeyApplier(hotkeyApplier)
{
    setWindowTitle(tr("Clipman 设置"));
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);

    auto *generalGroup = new QGroupBox(tr("通用"), this);
    auto *generalLayout = new QVBoxLayout(generalGroup);
    auto *generalForm = new QFormLayout;

    m_autostartCheck = new QCheckBox(tr("开机时自动启动 Clipman"), generalGroup);
    generalForm->addRow(m_autostartCheck);

    m_retentionSpin = new QSpinBox(generalGroup);
    m_retentionSpin->setRange(Store::kMinRetention, Store::kMaxRetention);
    m_retentionSpin->setSingleStep(100);
    m_retentionSpin->setSuffix(tr(" 条"));
    generalForm->addRow(tr("保留条数"), m_retentionSpin);

    m_hotkeyEdit = new QKeySequenceEdit(generalGroup);
    generalForm->addRow(tr("全局快捷键"), m_hotkeyEdit);
    generalLayout->addLayout(generalForm);

    auto *hotkeyHint = new QLabel(
        tr("需包含 Ctrl / Alt / "
           "Super，仅支持单组组合键；系统或其他应用已占用的组合无法录制，请换一个空闲组合。"),
        generalGroup);
    hotkeyHint->setWordWrap(true);
    generalLayout->addWidget(hotkeyHint);
    layout->addWidget(generalGroup);

    auto *updateGroup = new QGroupBox(tr("更新"), this);
    auto *updateLayout = new QVBoxLayout(updateGroup);
    auto *versionLabel =
        new QLabel(tr("当前版本：%1").arg(QApplication::applicationVersion()), updateGroup);
    updateLayout->addWidget(versionLabel);

    m_checkButton = new QPushButton(tr("检查更新"), updateGroup);
    updateLayout->addWidget(m_checkButton, 0, Qt::AlignLeft);

    m_statusLabel = new QLabel(tr("点击「检查更新」获取 GitHub 上的最新版本。"), updateGroup);
    m_statusLabel->setWordWrap(true);
    updateLayout->addWidget(m_statusLabel);
    layout->addWidget(updateGroup);

    auto *networkGroup = new QGroupBox(tr("网络"), this);
    auto *form = new QFormLayout(networkGroup);

    m_proxyMode = new QComboBox(networkGroup);
    m_proxyMode->addItem(tr("跟随系统 / 环境变量"), QStringLiteral("system"));
    m_proxyMode->addItem(tr("不使用代理"), QStringLiteral("none"));
    m_proxyMode->addItem(tr("手动配置"), QStringLiteral("manual"));
    form->addRow(tr("代理"), m_proxyMode);

    m_proxyType = new QComboBox(networkGroup);
    m_proxyType->addItem(QStringLiteral("HTTP"), QStringLiteral("http"));
    m_proxyType->addItem(QStringLiteral("SOCKS5"), QStringLiteral("socks5"));
    form->addRow(tr("类型"), m_proxyType);

    m_proxyHost = new QLineEdit(networkGroup);
    m_proxyHost->setPlaceholderText(QStringLiteral("127.0.0.1"));
    form->addRow(tr("地址"), m_proxyHost);

    m_proxyPort = new QSpinBox(networkGroup);
    m_proxyPort->setRange(1, 65535);
    form->addRow(tr("端口"), m_proxyPort);

    auto *hint = new QLabel(tr("代理仅用于检查更新与下载更新包。GitHub 访问受限时请在此配置。"),
                            networkGroup);
    hint->setWordWrap(true);
    form->addRow(hint);
    layout->addWidget(networkGroup);

    auto *buttons = new QHBoxLayout;
    m_closeButton = new QPushButton(tr("关闭"), this);
    buttons->addStretch();
    buttons->addWidget(m_closeButton);
    layout->addLayout(buttons);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_checkButton, &QPushButton::clicked, this, &SettingsDialog::checkForUpdates);
    connect(m_proxyMode, &QComboBox::currentIndexChanged, this, [this] { updateProxyControls(); });
    connect(m_autostartCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        QString error;
        if (m_autostart.setEnabled(enabled, &error))
            return;
        QMessageBox::warning(this, tr("设置开机自启失败"), error);
        const QSignalBlocker blocker(m_autostartCheck);
        m_autostartCheck->setChecked(!enabled);
    });

    connect(m_hotkeyEdit, &QKeySequenceEdit::keySequenceChanged, this,
            [this](const QKeySequence &sequence) {
                // Qt 6.4 没有 setMaximumSequenceLength()，手动截断为单组组合键。
                if (sequence.count() <= 1)
                    return;
                const QSignalBlocker blocker(m_hotkeyEdit);
                m_hotkeyEdit->setKeySequence(QKeySequence(sequence[0]));
            });
    connect(m_hotkeyEdit, &QKeySequenceEdit::editingFinished, this, [this] {
        const QString sequence = m_hotkeyEdit->keySequence().toString(QKeySequence::PortableText);
        if (sequence == m_appliedSequence)
            return;
        if (!applyHotkeySequence(sequence))
            restoreHotkeyEdit();
    });

    loadSettings();

    m_checker = new UpdateChecker(this);
    connect(m_checker, &UpdateChecker::updateAvailable, this, &SettingsDialog::onUpdateAvailable);
    connect(m_checker, &UpdateChecker::upToDate, this, [this](const QString &latest) {
        setBusy(false);
        if (latest == QApplication::applicationVersion())
            setStatus(tr("当前已是最新版本（%1）。").arg(latest));
        else
            setStatus(tr("当前版本 %1 不低于线上版本 %2。")
                          .arg(QApplication::applicationVersion(), latest));
    });
    connect(m_checker, &UpdateChecker::checkFailed, this, [this](const QString &reason) {
        setBusy(false);
        setStatus(reason);
        QMessageBox::warning(this, tr("检查更新失败"), reason);
    });
    connect(m_checker, &UpdateChecker::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total > 0)
                    setStatus(tr("正在下载更新… %1%").arg(received * 100 / total));
                else
                    setStatus(tr("正在下载更新… %1 KB").arg(received / 1024));
            });
    connect(m_checker, &UpdateChecker::downloadFinished, this, [this](const QString &path) {
        m_downloadPath = path;
        startInstall();
    });
    connect(m_checker, &UpdateChecker::downloadFailed, this, [this](const QString &reason) {
        setBusy(false);
        setStatus(reason);
        QMessageBox::warning(this, tr("下载失败"), reason);
    });
}

void SettingsDialog::done(int result)
{
    // 兜底：用户在完成计时器触发前就关闭对话框时，仍同步校验并应用最后一次修改。
    const QString sequence = m_hotkeyEdit->keySequence().toString(QKeySequence::PortableText);
    if (sequence != m_appliedSequence && !applyHotkeySequence(sequence))
        restoreHotkeyEdit();

    saveSettings();
    QDialog::done(result);
}

bool SettingsDialog::applyHotkeySequence(const QString &sequence)
{
    // 没有按下有效组合（例如点进输入框后直接失焦）时静默还原，不弹窗打扰。
    if (sequence.isEmpty()) {
        restoreHotkeyEdit();
        return false;
    }

    if (m_hotkeyApplier) {
        QString error;
        if (!m_hotkeyApplier(sequence, &error)) {
            if (error.isEmpty())
                error = tr("组合键「%1」不可用。").arg(sequence);

            QMessageBox box(this);
            box.setWindowTitle(tr("快捷键不可用"));
            box.setIcon(QMessageBox::Warning);
            box.setText(error);
            box.setInformativeText(
                m_appliedSequence.isEmpty()
                    ? tr("请换一个组合键，例如 Ctrl+Alt+V。")
                    : tr("请换一个组合键；当前仍使用「%1」。").arg(m_appliedSequence));
            box.setStandardButtons(QMessageBox::Ok);
            box.button(QMessageBox::Ok)->setText(tr("知道了"));
            box.exec();
            return false;
        }
    }

    m_appliedSequence = sequence;
    return true;
}

void SettingsDialog::restoreHotkeyEdit()
{
    const QSignalBlocker blocker(m_hotkeyEdit);
    m_hotkeyEdit->setKeySequence(
        QKeySequence::fromString(m_appliedSequence, QKeySequence::PortableText));
}

void SettingsDialog::loadSettings()
{
    QSettings settings;
    const QString mode =
        settings.value(QStringLiteral("network/proxyMode"), QStringLiteral("system")).toString();
    const int modeIndex = m_proxyMode->findData(mode);
    m_proxyMode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);

    const QString type =
        settings.value(QStringLiteral("network/proxyType"), QStringLiteral("http")).toString();
    const int typeIndex = m_proxyType->findData(type);
    m_proxyType->setCurrentIndex(typeIndex >= 0 ? typeIndex : 0);

    m_proxyHost->setText(
        settings.value(QStringLiteral("network/proxyHost"), QStringLiteral("127.0.0.1"))
            .toString());
    m_proxyPort->setValue(settings.value(QStringLiteral("network/proxyPort"), 7890).toInt());

    m_retentionSpin->setValue(
        settings.value(QStringLiteral("history/maxEntries"), Store::kDefaultRetention).toInt());

    m_appliedSequence =
        settings
            .value(QStringLiteral("hotkey/sequence"), QString::fromLatin1(kDefaultHotkeySequence))
            .toString();
    {
        const QSignalBlocker blocker(m_hotkeyEdit);
        m_hotkeyEdit->setKeySequence(
            QKeySequence::fromString(m_appliedSequence, QKeySequence::PortableText));
    }

    updateProxyControls();
    const QSignalBlocker blocker(m_autostartCheck);
    m_autostartCheck->setChecked(m_autostart.isEnabled());
}

void SettingsDialog::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("network/proxyMode"), m_proxyMode->currentData().toString());
    settings.setValue(QStringLiteral("network/proxyType"), m_proxyType->currentData().toString());
    settings.setValue(QStringLiteral("network/proxyHost"), m_proxyHost->text().trimmed());
    settings.setValue(QStringLiteral("network/proxyPort"), m_proxyPort->value());
    settings.setValue(QStringLiteral("history/maxEntries"), m_retentionSpin->value());
    // 只保存已成功应用的序列，避免把校验失败的键位写进配置。
    settings.setValue(QStringLiteral("hotkey/sequence"), m_appliedSequence);
    settings.sync();
}

void SettingsDialog::updateProxyControls()
{
    const bool manual = m_proxyMode->currentData().toString() == QLatin1String("manual");
    m_proxyType->setEnabled(manual);
    m_proxyHost->setEnabled(manual);
    m_proxyPort->setEnabled(manual);
}

QNetworkProxy SettingsDialog::resolvedProxy() const
{
    const QString mode = m_proxyMode->currentData().toString();
    if (mode == QLatin1String("none"))
        return QNetworkProxy(QNetworkProxy::NoProxy);

    if (mode == QLatin1String("manual")) {
        const QString host = m_proxyHost->text().trimmed();
        if (host.isEmpty())
            return QNetworkProxy(QNetworkProxy::NoProxy);

        const QNetworkProxy::ProxyType type =
            m_proxyType->currentData().toString() == QLatin1String("socks5")
                ? QNetworkProxy::Socks5Proxy
                : QNetworkProxy::HttpProxy;
        return QNetworkProxy(type, host, static_cast<quint16>(m_proxyPort->value()));
    }

    // 跟随系统：优先环境变量 / Qt 系统配置，取不到再读 GNOME 桌面代理。
    const QList<QNetworkProxy> proxies = QNetworkProxyFactory::systemProxyForQuery(
        QNetworkProxyQuery(QUrl(QStringLiteral("https://api.github.com"))));
    if (!proxies.isEmpty() && !proxies.first().hostName().isEmpty())
        return proxies.first();

    const QNetworkProxy desktopProxy = gnomeProxy();
    if (desktopProxy.type() != QNetworkProxy::NoProxy)
        return desktopProxy;
    return QNetworkProxy(QNetworkProxy::NoProxy);
}

void SettingsDialog::checkForUpdates()
{
    if (m_busy)
        return;

    saveSettings();
    m_checker->setProxy(resolvedProxy());

    setBusy(true);
    setStatus(tr("正在检查更新…"));
    m_checker->checkLatest(QString::fromLatin1(kRepoOwner), QString::fromLatin1(kRepoName),
                           QApplication::applicationVersion());
}

void SettingsDialog::onUpdateAvailable(const ReleaseInfo &info)
{
    setBusy(false);
    m_pending = info;
    setStatus(tr("发现新版本 %1。").arg(info.version));

    QString notes = info.notes.trimmed();
    if (notes.size() > 800)
        notes = notes.left(800) + QStringLiteral("…");

    QMessageBox box(this);
    box.setWindowTitle(tr("发现新版本"));
    box.setIcon(QMessageBox::Information);
    box.setText(
        tr("发现新版本 %1，当前版本 %2。").arg(info.version, QApplication::applicationVersion()));
    box.setInformativeText(notes.isEmpty() ? tr("是否下载并安装？")
                                           : notes + QStringLiteral("\n\n是否下载并安装？"));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.button(QMessageBox::Yes)->setText(tr("下载并安装"));
    box.button(QMessageBox::No)->setText(tr("暂不"));

    if (box.exec() != QMessageBox::Yes) {
        setStatus(tr("已取消更新。"));
        return;
    }

    startDownload();
}

void SettingsDialog::startDownload()
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (cacheDir.isEmpty())
        cacheDir = QDir::tempPath() + QStringLiteral("/clipman");
    const QString updateDir = QDir(cacheDir).filePath(QStringLiteral("updates"));
    QDir().mkpath(updateDir);

    m_downloadPath =
        QDir(updateDir).filePath(QStringLiteral("clipman_%1_amd64.deb").arg(m_pending.version));
    QFile::remove(m_downloadPath);

    setBusy(true);
    setStatus(tr("正在下载更新…"));
    m_checker->download(m_pending, m_downloadPath);
}

void SettingsDialog::startInstall()
{
    if (!QFile::exists(m_downloadPath)) {
        setBusy(false);
        setStatus(tr("下载文件不存在，更新已取消。"));
        return;
    }

    setBusy(true);
    setStatus(tr("下载完成，正在等待管理员授权…"));

    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("pkexec"));
    process->setArguments({QStringLiteral("apt-get"), QStringLiteral("install"),
                           QStringLiteral("-y"), m_downloadPath});

    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        const QString path = m_downloadPath;
        process->deleteLater();
        setBusy(false);
        setStatus(tr("无法启动安装程序。"));
        QMessageBox::warning(this, tr("安装失败"),
                             tr("未找到 pkexec，请手动执行：\nsudo apt install %1").arg(path));
    });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
                process->deleteLater();
                setBusy(false);

                if (status == QProcess::NormalExit && exitCode == 0) {
                    QFile::remove(m_downloadPath);
                    setStatus(tr("更新已安装，请重新启动 Clipman。"));
                    QMessageBox::information(this, tr("更新完成"),
                                             tr("更新已安装，请退出并重新启动 Clipman。"));
                    return;
                }

                if (exitCode == 126 || exitCode == 127) {
                    setStatus(tr("未获得授权，更新已取消。"));
                    QMessageBox::information(this, tr("安装已取消"),
                                             tr("未获得管理员授权，更新未安装。"));
                    return;
                }

                setStatus(tr("安装失败（退出码 %1）。").arg(exitCode));
                QMessageBox::warning(this, tr("安装失败"),
                                     tr("退出码 %1。可手动执行：\nsudo apt install %2")
                                         .arg(exitCode)
                                         .arg(m_downloadPath));
            });

    process->start();
}

void SettingsDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_checkButton->setEnabled(!busy);
    m_closeButton->setEnabled(!busy);
}

void SettingsDialog::setStatus(const QString &text)
{
    m_statusLabel->setText(text);
}
