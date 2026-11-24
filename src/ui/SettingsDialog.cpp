#include "ui/SettingsDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
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

namespace {

constexpr auto kRepoOwner = "Louisyoung7";
constexpr auto kRepoName = "clipman";

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Clipman 设置"));
    setMinimumWidth(460);

    auto *layout = new QVBoxLayout(this);

    auto *generalGroup = new QGroupBox(tr("通用"), this);
    auto *generalLayout = new QVBoxLayout(generalGroup);
    m_autostartCheck = new QCheckBox(tr("开机时自动启动 Clipman"), generalGroup);
    generalLayout->addWidget(m_autostartCheck);
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
    saveSettings();
    QDialog::done(result);
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

    // 跟随系统：读取 http_proxy/https_proxy 或桌面环境配置。
    const QList<QNetworkProxy> proxies = QNetworkProxyFactory::systemProxyForQuery(
        QNetworkProxyQuery(QUrl(QStringLiteral("https://api.github.com"))));
    return proxies.isEmpty() ? QNetworkProxy(QNetworkProxy::NoProxy) : proxies.first();
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
