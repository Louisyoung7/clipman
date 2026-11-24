#include "core/UpdateChecker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVector>

namespace {

QString userAgent()
{
    return QStringLiteral("Clipman-Updater/%1").arg(QCoreApplication::applicationVersion());
}

QVector<int> parseVersion(const QString &text)
{
    QString value = text.trimmed();
    if (value.startsWith(QLatin1Char('v')) || value.startsWith(QLatin1Char('V')))
        value.remove(0, 1);
    // 忽略 -rc1 之类的预发布后缀，只比较主版本号。
    value = value.section(QLatin1Char('-'), 0, 0);
    if (value.isEmpty())
        return {};

    QVector<int> parts;
    const QStringList segments = value.split(QLatin1Char('.'));
    for (const QString &segment : segments) {
        bool ok = false;
        const int number = segment.toInt(&ok);
        if (!ok || number < 0)
            return {};
        parts.append(number);
    }
    return parts;
}

} // namespace

bool ReleaseInfo::isValid() const
{
    return !tagName.isEmpty() && debUrl.isValid();
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
{
}

void UpdateChecker::checkLatest(const QString &owner, const QString &repo,
                                const QString &currentVersion)
{
    if (m_checkReply)
        return;

    const QUrl url(
        QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest").arg(owner, repo));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(15000);

    m_checkReply = m_network.get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, [this, currentVersion] {
        QNetworkReply *reply = m_checkReply;
        m_checkReply = nullptr;
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            if (status == 404) {
                emit checkFailed(tr("未找到发布信息（HTTP 404），请确认仓库已公开"));
            } else if (status == 403) {
                emit checkFailed(tr("GitHub API 访问受限（HTTP 403），请稍后再试"));
            } else {
                emit checkFailed(tr("检查更新失败：%1").arg(reply->errorString()));
            }
            return;
        }

        const ReleaseInfo info = parseLatestRelease(reply->readAll());
        if (!info.isValid()) {
            emit checkFailed(tr("无法解析发布信息"));
            return;
        }

        if (isNewer(info.version, currentVersion))
            emit updateAvailable(info);
        else
            emit upToDate(info.version);
    });
}

void UpdateChecker::download(const ReleaseInfo &info, const QString &targetPath)
{
    if (m_downloadReply || !info.isValid())
        return;

    m_downloadPath = targetPath;
    m_downloadFile = new QFile(targetPath);
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString reason = m_downloadFile->errorString();
        delete m_downloadFile;
        m_downloadFile = nullptr;
        emit downloadFailed(tr("无法写入下载文件：%1").arg(reason));
        return;
    }

    QNetworkRequest request(info.debUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setTransferTimeout(300000);

    m_downloadReply = m_network.get(request);
    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this] {
        if (m_downloadFile)
            m_downloadFile->write(m_downloadReply->readAll());
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this,
            &UpdateChecker::downloadProgress);
    connect(m_downloadReply, &QNetworkReply::finished, this, [this, info] {
        QNetworkReply *reply = m_downloadReply;
        m_downloadReply = nullptr;
        reply->deleteLater();

        if (m_downloadFile) {
            m_downloadFile->write(reply->readAll());
            m_downloadFile->close();
            delete m_downloadFile;
            m_downloadFile = nullptr;
        }

        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(m_downloadPath);
            emit downloadFailed(tr("下载失败：%1").arg(reply->errorString()));
            return;
        }

        if (info.debSize > 0 && QFileInfo(m_downloadPath).size() != info.debSize) {
            QFile::remove(m_downloadPath);
            emit downloadFailed(tr("下载文件大小不符，已删除"));
            return;
        }

        if (info.checksumUrl.isValid())
            verifyChecksum(info, m_downloadPath);
        else
            emit downloadFinished(m_downloadPath);
    });
}

void UpdateChecker::verifyChecksum(const ReleaseInfo &info, const QString &path)
{
    QNetworkRequest request(info.checksumUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setTransferTimeout(30000);

    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path] {
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(path);
            emit downloadFailed(tr("下载校验文件失败：%1").arg(reply->errorString()));
            return;
        }

        // sha256sum 输出首列是摘要，忽略后面的文件名。
        const QByteArray expected = reply->readAll().trimmed().split(' ').value(0).toLower();
        QFile file(path);
        if (expected.isEmpty() || !file.open(QIODevice::ReadOnly)) {
            QFile::remove(path);
            emit downloadFailed(tr("校验文件无效，更新包已删除"));
            return;
        }

        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&file);
        file.close();

        if (hash.result().toHex() != expected) {
            QFile::remove(path);
            emit downloadFailed(tr("更新包校验失败，已删除下载文件"));
            return;
        }

        emit downloadFinished(path);
    });
}

bool UpdateChecker::isNewer(const QString &candidate, const QString &current)
{
    const QVector<int> lhs = parseVersion(candidate);
    const QVector<int> rhs = parseVersion(current);
    if (lhs.isEmpty() || rhs.isEmpty())
        return false;

    const int count = qMax(lhs.size(), rhs.size());
    for (int i = 0; i < count; ++i) {
        const int left = i < lhs.size() ? lhs.at(i) : 0;
        const int right = i < rhs.size() ? rhs.at(i) : 0;
        if (left != right)
            return left > right;
    }
    return false;
}

ReleaseInfo UpdateChecker::parseLatestRelease(const QByteArray &json)
{
    ReleaseInfo info;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return info;

    const QJsonObject root = doc.object();
    info.tagName = root.value(QStringLiteral("tag_name")).toString();
    info.name = root.value(QStringLiteral("name")).toString();
    info.notes = root.value(QStringLiteral("body")).toString();
    if (info.tagName.isEmpty())
        return ReleaseInfo();

    info.version = info.tagName;
    if (info.version.startsWith(QLatin1Char('v')) || info.version.startsWith(QLatin1Char('V')))
        info.version.remove(0, 1);

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    QString debName;
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString assetName = asset.value(QStringLiteral("name")).toString();
        if (!assetName.endsWith(QStringLiteral(".deb")))
            continue;
        debName = assetName;
        info.debUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        info.debSize = static_cast<qint64>(asset.value(QStringLiteral("size")).toDouble());
        break;
    }

    if (!info.debUrl.isValid() || info.debUrl.isEmpty())
        return ReleaseInfo();

    // 校验文件可有可无：优先同名 .sha256，其次 checksums.sha256。
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString assetName = asset.value(QStringLiteral("name")).toString();
        const bool matches =
            assetName == debName + QStringLiteral(".sha256") ||
            assetName == QStringLiteral("checksums.sha256") ||
            (assetName.endsWith(QStringLiteral(".sha256")) && assetName.contains(debName));
        if (!matches)
            continue;
        info.checksumUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
        break;
    }

    return info;
}
