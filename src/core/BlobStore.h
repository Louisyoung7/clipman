#pragma once

#include <QByteArray>
#include <QString>

// 图片二进制文件的落盘管理。只依赖 QtCore，不碰数据库与 Widgets。
class BlobStore
{
public:
    BlobStore() = default;
    explicit BlobStore(QString dir);

    bool initialize(QString *error = nullptr);

    QString dir() const { return m_dir; }
    QString thumbnailPath(const QString &hash) const;

    QString saveImage(const QString &hash, const QByteArray &png, QString *error = nullptr) const;
    QString saveThumbnail(const QString &hash, const QByteArray &png,
                          QString *error = nullptr) const;

    // 删除 <hash>.*（主图与缩略图一并清掉）
    void remove(const QString &hash) const;
    void clear() const;

private:
    QString imagePath(const QString &hash) const;
    QString writeFile(const QString &path, const QByteArray &bytes, QString *error) const;

    QString m_dir;
};
