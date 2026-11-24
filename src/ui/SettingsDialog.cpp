#include "ui/SettingsDialog.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
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
    setMinimumWidth(420);

    auto *layout = new QVBoxLayout(this);

    auto *versionLabel =
        new QLabel(tr("当前版本：%1").arg(QApplication::applicationVersion()), this);
    layout->addWidget(versionLabel);

    m_statusLabel = new QLabel(tr("点击「检查更新」获取 GitHub 上的最新版本。"), this);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto *buttons = new QHBoxLayout;
    m_checkButton = new QPushButton(tr("检查更新"), this);
    m_closeButton = new QPushButton(tr("关闭"), this);
    buttons->addWidget(m_checkButton);
    buttons->addStretch();
    buttons->addWidget(m_closeButton);
    layout->addLayout(buttons);

    connect(m_checkButton, &QPushButton::clicked, this, &SettingsDialog::checkForUpdates);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);

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

void SettingsDialog::checkForUpdates()
{
    if (m_busy)
        return;

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
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_downloadPath =
        QDir(tempDir).filePath(QStringLiteral("clipman_%1_amd64.deb").arg(m_pending.version));
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
