#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QUrl>

class QFile;
class QNetworkReply;

struct ReleaseInfo
{
    QString tagName;
    QString version;
    QString name;
    QString notes;
    QUrl debUrl;
    qint64 debSize = 0;

    bool isValid() const;
};

Q_DECLARE_METATYPE(ReleaseInfo)

// 检查 GitHub Releases 并下载 deb。全部走 HTTPS，不保存任何凭据。
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    void checkLatest(const QString &owner, const QString &repo, const QString &currentVersion);
    void download(const ReleaseInfo &info, const QString &targetPath);

    // 语义化版本比较：仅当 candidate > current 时返回 true；解析失败一律视为无更新。
    static bool isNewer(const QString &candidate, const QString &current);
    static ReleaseInfo parseLatestRelease(const QByteArray &json);

signals:
    void updateAvailable(const ReleaseInfo &info);
    void upToDate(const QString &latestVersion);
    void checkFailed(const QString &reason);
    void downloadProgress(qint64 received, qint64 total);
    void downloadFinished(const QString &path);
    void downloadFailed(const QString &reason);

private:
    QNetworkAccessManager m_network;
    QNetworkReply *m_checkReply = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    QFile *m_downloadFile = nullptr;
    QString m_downloadPath;
};
