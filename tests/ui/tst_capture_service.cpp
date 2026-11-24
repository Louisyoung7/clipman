#include <QtTest>

#include <QBuffer>
#include <QFileInfo>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "core/BlobStore.h"
#include "core/CaptureService.h"

class TestCaptureService : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void textIsHashed();
    void blankTextIsDropped();
    void filesAreJoined();
    void imageIsSaved();

private:
    BlobStore createBlobs();

    std::unique_ptr<QTemporaryDir> m_temp;
};

void TestCaptureService::init()
{
    m_temp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_temp->isValid());
}

void TestCaptureService::cleanup()
{
    m_temp.reset();
}

BlobStore TestCaptureService::createBlobs()
{
    BlobStore blobs(m_temp->path() + QStringLiteral("/blobs"));
    QString error;
    const bool ok = blobs.initialize(&error);
    if (!ok)
        qWarning() << "初始化 blobs 失败:" << error;
    return blobs;
}

void TestCaptureService::textIsHashed()
{
    BlobStore blobs = createBlobs();
    CaptureService service(blobs);
    QSignalSpy spy(&service, &CaptureService::ready);

    ClipboardItem item;
    item.kind = ClipKind::Text;
    item.text = QStringLiteral("hello");
    service.submit(item);

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const PreparedItem prepared = spy.at(0).at(0).value<PreparedItem>();
    QVERIFY(prepared.valid);
    QVERIFY(!prepared.hash.isEmpty());
    QCOMPARE(prepared.text, QStringLiteral("hello"));
    QCOMPARE(prepared.byteSize, 5);
}

void TestCaptureService::blankTextIsDropped()
{
    BlobStore blobs = createBlobs();
    CaptureService service(blobs);
    QSignalSpy spy(&service, &CaptureService::ready);

    ClipboardItem item;
    item.kind = ClipKind::Text;
    item.text = QStringLiteral("   \n\t");
    service.submit(item);

    QTest::qWait(300);
    QCOMPARE(spy.count(), 0);
}

void TestCaptureService::filesAreJoined()
{
    BlobStore blobs = createBlobs();
    CaptureService service(blobs);
    QSignalSpy spy(&service, &CaptureService::ready);

    ClipboardItem item;
    item.kind = ClipKind::Files;
    item.paths = {QString(), QStringLiteral("/tmp/a.txt"), QStringLiteral("/tmp/b.txt")};
    service.submit(item);

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const PreparedItem prepared = spy.at(0).at(0).value<PreparedItem>();
    QVERIFY(prepared.valid);
    QCOMPARE(prepared.text, QStringLiteral("/tmp/a.txt\n/tmp/b.txt"));
}

void TestCaptureService::imageIsSaved()
{
    BlobStore blobs = createBlobs();
    CaptureService service(blobs);
    QSignalSpy spy(&service, &CaptureService::ready);

    QImage image(10, 10, QImage::Format_RGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));

    ClipboardItem item;
    item.kind = ClipKind::Image;
    item.mime = QStringLiteral("image/png");
    item.data = png;
    service.submit(item);

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
    const PreparedItem prepared = spy.at(0).at(0).value<PreparedItem>();
    QVERIFY(prepared.valid);
    QCOMPARE(prepared.width, 10);
    QCOMPARE(prepared.height, 10);
    QVERIFY(QFileInfo::exists(prepared.path));
    QVERIFY(QFileInfo::exists(prepared.thumbPath));
}

QTEST_MAIN(TestCaptureService)

#include "tst_capture_service.moc"
