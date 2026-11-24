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
        "name": "Clipman v0.4.0",
        "body": "更新说明",
        "assets": [
            {"name": "clipman_0.4.0_amd64.deb.sha256", "size": 64,
             "browser_download_url": "https://example.com/clipman.deb.sha256"},
            {"name": "clipman_0.4.0_amd64.deb", "size": 102400,
             "browser_download_url": "https://example.com/clipman_0.4.0_amd64.deb"}
        ]
    })";

    const ReleaseInfo info = UpdateChecker::parseLatestRelease(json);
    REQUIRE(info.isValid());
    REQUIRE(info.tagName == QStringLiteral("v0.4.0"));
    REQUIRE(info.version == QStringLiteral("0.4.0"));
    REQUIRE(info.name == QStringLiteral("Clipman v0.4.0"));
    REQUIRE(info.notes == QStringLiteral("更新说明"));
    REQUIRE(info.debUrl == QUrl(QStringLiteral("https://example.com/clipman_0.4.0_amd64.deb")));
    REQUIRE(info.debSize == 102400);
}

TEST_CASE("UpdateChecker 对异常响应返回无效结果", "[update]")
{
    REQUIRE_FALSE(UpdateChecker::parseLatestRelease(QByteArray("not json")).isValid());
    REQUIRE_FALSE(UpdateChecker::parseLatestRelease("{}").isValid());
    REQUIRE_FALSE(UpdateChecker::parseLatestRelease(R"({"tag_name": "v0.4.0"})").isValid());

    const QByteArray noDeb = R"({
        "tag_name": "v0.4.0",
        "assets": [
            {"name": "clipman_0.4.0_amd64.rpm", "size": 1024,
             "browser_download_url": "https://example.com/clipman.rpm"}
        ]
    })";
    REQUIRE_FALSE(UpdateChecker::parseLatestRelease(noDeb).isValid());
}
