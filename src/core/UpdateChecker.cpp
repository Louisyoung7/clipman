#include "core/UpdateChecker.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace {

QString userAgent()
{
    return QStringLiteral("Clipman-Updater/%1").arg(QCoreApplication::applicationVersion());
}

QString versionFromTag(const QString &tagName)
{
    QString version = tagName;
    if (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V')))
        version.remove(0, 1);
    return version;
}

QNetworkRequest makeRequest(const QUrl &url, int timeoutMs)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setTransferTimeout(timeoutMs);
    return request;
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

void UpdateChecker::setProxy(const QNetworkProxy &proxy)
{
    m_network.setProxy(proxy);
}

void UpdateChecker::checkLatest(const QString &owner, const QString &repo,
                                const QString &currentVersion)
{
    if (m_checkReply || m_webReply)
        return;

    const QUrl url(
        QStringLiteral("https://api.github.com/repos/%1/%2/releases/latest").arg(owner, repo));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(15000);

    m_checkReply = m_network.get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, [this, owner, repo, currentVersion] {
        QNetworkReply *reply = m_checkReply;
        m_checkReply = nullptr;
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            const QString reason =
                httpFailureReason(status, reply->rawHeader("x-ratelimit-remaining"),
                                  reply->rawHeader("x-ratelimit-reset"),
                                  reply->rawHeader("retry-after"), reply->errorString());
            // 403/429 多为未登录 API 限流；API 不可用时改用网页接口，避免额度耗尽即无法更新。
            if (status == 403 || status == 429) {
                startWebFallback(owner, repo, currentVersion, reason);
                return;
            }
            emit checkFailed(reason);
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

void UpdateChecker::startWebFallback(const QString &owner, const QString &repo,
                                     const QString &currentVersion, const QString &apiFailure)
{
    const QUrl url(QStringLiteral("https://github.com/%1/%2/releases/latest").arg(owner, repo));
    m_webReply = m_network.get(makeRequest(url, 15000));
    connect(m_webReply, &QNetworkReply::finished, this,
            [this, owner, repo, currentVersion, apiFailure] {
                QNetworkReply *reply = m_webReply;
                m_webReply = nullptr;
                reply->deleteLater();

                // /releases/latest 会 302 到 /releases/tag/<tag>，不占用 API 速率额度。
                const QString tagName = reply->error() == QNetworkReply::NoError
                                            ? tagFromReleaseUrl(reply->url())
                                            : QString();
                if (tagName.isEmpty()) {
                    const QString detail = reply->error() == QNetworkReply::NoError
                                               ? tr("未找到发布版本")
                                               : reply->errorString();
                    emit checkFailed(tr("%1\n备用检查也失败：%2").arg(apiFailure, detail));
                    return;
                }

                // 资产列表由单独的网页片段返回，同样不消耗 API 额度。
                const QUrl assetsUrl(
                    QStringLiteral("https://github.com/%1/%2/releases/expanded_assets/%3")
                        .arg(owner, repo, QString::fromLatin1(QUrl::toPercentEncoding(tagName))));
                m_webReply = m_network.get(makeRequest(assetsUrl, 15000));
                connect(
                    m_webReply, &QNetworkReply::finished, this,
                    [this, tagName, currentVersion, apiFailure] {
                        QNetworkReply *assetsReply = m_webReply;
                        m_webReply = nullptr;
                        assetsReply->deleteLater();

                        const bool ok = assetsReply->error() == QNetworkReply::NoError;
                        const ReleaseInfo info =
                            ok ? parseAssetsHtml(assetsReply->readAll(), tagName) : ReleaseInfo();
                        if (!info.isValid()) {
                            const QString detail =
                                ok ? tr("发布页格式无法解析") : assetsReply->errorString();
                            emit checkFailed(tr("%1\n备用检查也失败：%2").arg(apiFailure, detail));
                            return;
                        }

                        if (isNewer(info.version, currentVersion))
                            emit updateAvailable(info);
                        else
                            emit upToDate(info.version);
                    });
            });
}

QString UpdateChecker::tagFromReleaseUrl(const QUrl &url)
{
    const QString marker = QStringLiteral("/releases/tag/");
    const QString path = url.path();
    const int index = path.indexOf(marker);
    if (index < 0)
        return {};

    QString tagName = QUrl::fromPercentEncoding(path.mid(index + marker.size()).toUtf8());
    while (tagName.endsWith(QLatin1Char('/')))
        tagName.chop(1);
    return tagName;
}

ReleaseInfo UpdateChecker::parseAssetsHtml(const QByteArray &html, const QString &tagName)
{
    if (tagName.isEmpty())
        return {};

    static const QRegularExpression hrefPattern(QStringLiteral("href=\"([^\"]+)\""));
    const QString text = QString::fromUtf8(html);
    QStringList downloads;
    auto it = hrefPattern.globalMatch(text);
    while (it.hasNext()) {
        const QString href = it.next().captured(1);
        if (href.contains(QLatin1String("/releases/download/")))
            downloads.append(href);
    }

    QString debPath;
    for (const QString &href : downloads) {
        if (href.endsWith(QLatin1String(".deb"))) {
            debPath = href;
            break;
        }
    }
    if (debPath.isEmpty())
        return {};

    // 与 API 路径一致：优先 <deb>.sha256，其次 checksums.sha256，最后任意 sha256。
    const QString debName = debPath.section(QLatin1Char('/'), -1);
    QString checksumPath;
    const QStringList preferred = {debName + QStringLiteral(".sha256"),
                                   QStringLiteral("checksums.sha256")};
    for (const QString &name : preferred) {
        for (const QString &href : downloads) {
            if (href.section(QLatin1Char('/'), -1) == name) {
                checksumPath = href;
                break;
            }
        }
        if (!checksumPath.isEmpty())
            break;
    }
    if (checksumPath.isEmpty()) {
        for (const QString &href : downloads) {
            if (href.endsWith(QLatin1String(".sha256"))) {
                checksumPath = href;
                break;
            }
        }
    }

    const QUrl base(QStringLiteral("https://github.com"));
    ReleaseInfo info;
    info.tagName = tagName;
    info.version = versionFromTag(tagName);
    info.debUrl = base.resolved(QUrl(debPath));
    if (!info.debUrl.isValid() || info.debUrl.isEmpty())
        return {};
    if (!checksumPath.isEmpty())
        info.checksumUrl = base.resolved(QUrl(checksumPath));
    return info;
}

QString UpdateChecker::httpFailureReason(int status, const QByteArray &rateRemaining,
                                         const QByteArray &rateReset, const QByteArray &retryAfter,
                                         const QString &errorText)
{
    if (status == 404)
        return tr("未找到发布信息（HTTP 404），请确认仓库已公开");

    if (status == 403 || status == 429) {
        bool ok = false;
        const qint64 reset = rateReset.trimmed().toLongLong(&ok);
        if (ok && rateRemaining.trimmed() == QByteArrayLiteral("0")) {
            const QString resume =
                QDateTime::fromSecsSinceEpoch(reset).toString(QStringLiteral("HH:mm"));
            return tr("GitHub API 速率限制已用尽（未登录每小时 60 次），%1 后恢复").arg(resume);
        }
        const qint64 wait = retryAfter.trimmed().toLongLong(&ok);
        if (ok && wait > 0)
            return tr("GitHub 要求稍后重试（HTTP %1），请等待 %2 秒后再试").arg(status).arg(wait);
        return tr("GitHub 拒绝访问（HTTP %1），可能是网络或代理拦截").arg(status);
    }

    return errorText.isEmpty() ? tr("检查更新失败") : tr("检查更新失败：%1").arg(errorText);
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

    info.version = versionFromTag(info.tagName);

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
