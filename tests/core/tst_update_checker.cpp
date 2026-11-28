#include <catch2/catch_test_macros.hpp>

#include "core/UpdateChecker.h"

TEST_CASE("UpdateChecker 版本比较遵循语义化版本", "[update]")
{
    REQUIRE(UpdateChecker::isNewer(QStringLiteral("v0.4.0"), QStringLiteral("0.3.0")));
    REQUIRE(UpdateChecker::isNewer(QStringLiteral("0.10.0"), QStringLiteral("0.9.9")));
    REQUIRE(UpdateChecker::isNewer(QStringLiteral("1.0.0"), QStringLiteral("0.99.99")));
    REQUIRE(UpdateChecker::isNewer(QStringLiteral("0.4.0-rc1"), QStringLiteral("0.3.0")));

    REQUIRE_FALSE(UpdateChecker::isNewer(QStringLiteral("0.3.0"), QStringLiteral("0.3.0")));
    REQUIRE_FALSE(UpdateChecker::isNewer(QStringLiteral("0.3.0"), QStringLiteral("0.3.1")));
    REQUIRE_FALSE(UpdateChecker::isNewer(QStringLiteral("v0.3"), QStringLiteral("0.3.0")));
    REQUIRE_FALSE(UpdateChecker::isNewer(QStringLiteral("nightly"), QStringLiteral("0.3.0")));
    REQUIRE_FALSE(UpdateChecker::isNewer(QStringLiteral("0.4.0"), QString()));
}

TEST_CASE("UpdateChecker 解析最新 release 并选中最 deb 资产", "[update]")
{
    const QByteArray json = R"({
        "tag_name": "v0.4.0",
        "name": "Clipory v0.4.0",
        "body": "更新说明",
        "assets": [
            {"name": "clipory_0.4.0_amd64.deb.sha256", "size": 64,
             "browser_download_url": "https://example.com/clipory.deb.sha256"},
            {"name": "clipory_0.4.0_amd64.deb", "size": 102400,
             "browser_download_url": "https://example.com/clipory_0.4.0_amd64.deb"}
        ]
    })";

    const ReleaseInfo info = UpdateChecker::parseLatestRelease(json);
    REQUIRE(info.isValid());
    REQUIRE(info.tagName == QStringLiteral("v0.4.0"));
    REQUIRE(info.version == QStringLiteral("0.4.0"));
    REQUIRE(info.name == QStringLiteral("Clipory v0.4.0"));
    REQUIRE(info.notes == QStringLiteral("更新说明"));
    REQUIRE(info.debUrl == QUrl(QStringLiteral("https://example.com/clipory_0.4.0_amd64.deb")));
    REQUIRE(info.debSize == 102400);
    REQUIRE(info.checksumUrl == QUrl(QStringLiteral("https://example.com/clipory.deb.sha256")));
}

TEST_CASE("UpdateChecker 兼容独立的 checksums.sha256", "[update]")
{
    const QByteArray json = R"({
        "tag_name": "v0.4.1",
        "assets": [
            {"name": "clipory_0.4.1_amd64.deb", "size": 2048,
             "browser_download_url": "https://example.com/clipory_0.4.1_amd64.deb"},
            {"name": "checksums.sha256", "size": 128,
             "browser_download_url": "https://example.com/checksums.sha256"}
        ]
    })";

    const ReleaseInfo info = UpdateChecker::parseLatestRelease(json);
    REQUIRE(info.isValid());
    REQUIRE(info.checksumUrl == QUrl(QStringLiteral("https://example.com/checksums.sha256")));
}

TEST_CASE("UpdateChecker 对异常响应返回无效结果", "[update]")
{
    REQUIRE_FALSE(UpdateChecker::parseLatestRelease(QByteArray("not json")).isValid());
    REQUIRE_FALSE(UpdateChecker::parseLatestRelease("{}").isValid());
    REQUIRE_FALSE(UpdateChecker::parseLatestRelease(R"({"tag_name": "v0.4.0"})").isValid());

    const QByteArray noDeb = R"({
        "tag_name": "v0.4.0",
        "assets": [
            {"name": "clipory_0.4.0_amd64.rpm", "size": 1024,
             "browser_download_url": "https://example.com/clipory.rpm"}
        ]
    })";
    REQUIRE_FALSE(UpdateChecker::parseLatestRelease(noDeb).isValid());
}

TEST_CASE("UpdateChecker 从网页重定向地址提取 tag", "[update]")
{
    REQUIRE(UpdateChecker::tagFromReleaseUrl(QUrl(QStringLiteral(
                "https://github.com/o/r/releases/tag/v0.5.0"))) == QStringLiteral("v0.5.0"));
    REQUIRE(UpdateChecker::tagFromReleaseUrl(QUrl(QStringLiteral(
                "https://github.com/o/r/releases/tag/v0.5.0/"))) == QStringLiteral("v0.5.0"));
    REQUIRE(UpdateChecker::tagFromReleaseUrl(
                QUrl(QStringLiteral("https://github.com/o/r/releases/latest")))
                .isEmpty());
}

TEST_CASE("UpdateChecker 从发布页 HTML 解析更新包", "[update]")
{
    const QByteArray html = R"(
        <a href="/o/r/releases/download/v0.6.0/checksums.sha256">checksums.sha256</a>
        <a href="/o/r/releases/download/v0.6.0/clipory_0.6.0_amd64.deb">clipory_0.6.0_amd64.deb</a>
        <a href="/o/r/archive/refs/tags/v0.6.0.tar.gz">Source code</a>)";

    const ReleaseInfo info = UpdateChecker::parseAssetsHtml(html, QStringLiteral("v0.6.0"));
    REQUIRE(info.isValid());
    REQUIRE(info.tagName == QStringLiteral("v0.6.0"));
    REQUIRE(info.version == QStringLiteral("0.6.0"));
    REQUIRE(info.debUrl ==
            QUrl(QStringLiteral(
                "https://github.com/o/r/releases/download/v0.6.0/clipory_0.6.0_amd64.deb")));
    REQUIRE(
        info.checksumUrl ==
        QUrl(QStringLiteral("https://github.com/o/r/releases/download/v0.6.0/checksums.sha256")));

    // 同名 .sha256 优先于 checksums.sha256。
    const QByteArray both = R"(
        <a href="/o/r/releases/download/v0.6.0/checksums.sha256">checksums.sha256</a>
        <a href="/o/r/releases/download/v0.6.0/clipory_0.6.0_amd64.deb">deb</a>
        <a href="/o/r/releases/download/v0.6.0/clipory_0.6.0_amd64.deb.sha256">deb.sha256</a>)";
    REQUIRE(UpdateChecker::parseAssetsHtml(both, QStringLiteral("v0.6.0")).checksumUrl ==
            QUrl(QStringLiteral(
                "https://github.com/o/r/releases/download/v0.6.0/clipory_0.6.0_amd64.deb.sha256")));

    REQUIRE_FALSE(UpdateChecker::parseAssetsHtml(html, QString()).isValid());
    REQUIRE_FALSE(
        UpdateChecker::parseAssetsHtml("<html></html>", QStringLiteral("v0.6.0")).isValid());
}

TEST_CASE("UpdateChecker 为 403 区分限流与拦截", "[update]")
{
    const QString limited = UpdateChecker::httpFailureReason(
        403, QByteArrayLiteral("0"), QByteArrayLiteral("1795526395"), QByteArray(),
        QStringLiteral("server replied"));
    REQUIRE(limited.contains(QStringLiteral("速率限制")));

    const QString retryLater = UpdateChecker::httpFailureReason(
        429, QByteArrayLiteral("10"), QByteArray(), QByteArrayLiteral("42"), QString());
    REQUIRE(retryLater.contains(QStringLiteral("42")));

    const QString blocked =
        UpdateChecker::httpFailureReason(403, {}, {}, {}, QStringLiteral("blocked"));
    REQUIRE(blocked.contains(QStringLiteral("403")));

    REQUIRE(UpdateChecker::httpFailureReason(404, {}, {}, {}, QString())
                .contains(QStringLiteral("404")));
}
