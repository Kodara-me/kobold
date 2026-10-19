// SPDX-License-Identifier: MIT

#include "shelfcovers.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace Kobold::ShelfCovers
{

namespace
{
QImage scaled(QImage image)
{
    if (image.height() > MaxHeight) {
        image = image.scaledToHeight(MaxHeight, Qt::SmoothTransformation);
    }
    return image;
}
}

QString cacheFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/kobold/shelves");
}

QString cachePath(qint64 bookId)
{
    return cacheFolder() + QStringLiteral("/%1.jpg").arg(bookId);
}

QString imagesFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/shelves/covers");
}

QImage cover(const ShelfBook &book, const Shelf &shelf)
{
    const BookMetadata metadata = book.effective();
    if (!metadata.coverImage.isEmpty()) {
        const QImage image(metadata.coverImage);
        if (!image.isNull()) {
            return scaled(image);
        }
    }
    const QString filePath = QDir(shelf.path).filePath(book.path);
    // A chosen page is cached under its own name.
    const QString path = metadata.coverPage >= 0 ? cacheFolder() + QStringLiteral("/%1-p%2.jpg").arg(book.id).arg(metadata.coverPage) : cachePath(book.id);
    const QFileInfo cached(path);
    if (cached.exists() && cached.lastModified().toUTC() >= book.modified) {
        QImage image(path);
        if (!image.isNull()) {
            return image;
        }
    }

    QImage image = metadata.coverPage >= 0 ? ShelfMetadata::readPage(filePath, metadata.coverPage) : QImage();
    if (image.isNull()) {
        image = ShelfMetadata::readCover(filePath);
    }
    if (image.isNull()) {
        return image;
    }
    image = scaled(image);
    QDir().mkpath(cacheFolder());
    image.convertToFormat(QImage::Format_RGB32).save(path, "JPG", 85);
    return image;
}

QImage seriesCover(const SeriesMetadata &series)
{
    if (series.coverImage.isEmpty()) {
        return {};
    }
    return scaled(QImage(series.coverImage));
}

void forget(qint64 bookId)
{
    QFile::remove(cachePath(bookId));
}

QString storeImage(const QByteArray &data, QString *errorString)
{
    const QImage image = QImage::fromData(data);
    if (image.isNull()) {
        *errorString = i18n("this is not an image Kobold can read");
        return {};
    }
    // Named after its content: the same image is kept once.
    const QString name = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex().left(16)) + QStringLiteral(".jpg");
    const QString path = QDir(imagesFolder()).filePath(name);
    QDir().mkpath(imagesFolder());
    if (!QFileInfo::exists(path) && !scaled(image).convertToFormat(QImage::Format_RGB32).save(path, "JPG", 90)) {
        *errorString = i18n("cannot write %1", path);
        return {};
    }
    return path;
}

QString storeImageFile(const QString &path, QString *errorString)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *errorString = i18n("cannot read %1", path);
        return {};
    }
    return storeImage(file.readAll(), errorString);
}

} // namespace Kobold::ShelfCovers
