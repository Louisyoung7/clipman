#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "core/BlobStore.h"

TEST_CASE("BlobStore 按哈希命名图片与缩略图", "[blobstore]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    BlobStore blobs(temp.path() + QStringLiteral("/blobs"));
    QString error;
    REQUIRE(blobs.initialize(&error));

    const QString imagePath = blobs.saveImage(QStringLiteral("abc"), QByteArray("png-data"));
    const QString thumbPath = blobs.saveThumbnail(QStringLiteral("abc"), QByteArray("thumb-data"));

    REQUIRE(imagePath == blobs.dir() + QStringLiteral("/abc.png"));
    REQUIRE(thumbPath == blobs.dir() + QStringLiteral("/abc.thumb.png"));
    REQUIRE(QFile::exists(imagePath));
    REQUIRE(QFile::exists(thumbPath));
}

TEST_CASE("BlobStore::remove 同时清理主图与缩略图", "[blobstore]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    BlobStore blobs(temp.path() + QStringLiteral("/blobs"));
    REQUIRE(blobs.initialize());

    blobs.saveImage(QStringLiteral("abc"), QByteArray("png-data"));
    blobs.saveThumbnail(QStringLiteral("abc"), QByteArray("thumb-data"));
    const QString other = blobs.saveImage(QStringLiteral("def"), QByteArray("other"));

    blobs.remove(QStringLiteral("abc"));

    REQUIRE(!QFile::exists(blobs.dir() + QStringLiteral("/abc.png")));
    REQUIRE(!QFile::exists(blobs.dir() + QStringLiteral("/abc.thumb.png")));
    REQUIRE(QFile::exists(other));
}

TEST_CASE("BlobStore::clear 清空目录但保留目录", "[blobstore]")
{
    QTemporaryDir temp;
    REQUIRE(temp.isValid());

    BlobStore blobs(temp.path() + QStringLiteral("/blobs"));
    REQUIRE(blobs.initialize());
    blobs.saveImage(QStringLiteral("abc"), QByteArray("png-data"));
    blobs.saveThumbnail(QStringLiteral("abc"), QByteArray("thumb-data"));

    blobs.clear();

    REQUIRE(QDir(blobs.dir()).exists());
    REQUIRE(QDir(blobs.dir()).entryList(QDir::Files).isEmpty());
}

TEST_CASE("BlobStore 空目录参数初始化失败", "[blobstore]")
{
    BlobStore blobs;
    QString error;
    REQUIRE(!blobs.initialize(&error));
    REQUIRE(!error.isEmpty());
}
