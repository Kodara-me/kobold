// SPDX-License-Identifier: MIT

#include "shelfedits.h"
#include "jsonfile.h"
#include "shelfcovers.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

namespace Kobold::ShelfEdits
{

namespace
{
// A cover image is kept among Kobold's covers: the original may go away.
bool keepCover(QString *coverImage, QString *errorString)
{
    if (coverImage->isEmpty() || coverImage->startsWith(ShelfCovers::imagesFolder() + QLatin1Char('/'))) {
        return true;
    }
    const QString stored = ShelfCovers::storeImageFile(*coverImage, errorString);
    if (stored.isEmpty()) {
        return false;
    }
    *coverImage = stored;
    return true;
}

QString imageData(const QString &path)
{
    QFile file(path);
    return !path.isEmpty() && file.open(QIODevice::ReadOnly) ? QString::fromLatin1(file.readAll().toBase64()) : QString();
}

// Exported covers carry their image; imported ones are stored again here.
QJsonObject exportCover(QJsonObject object)
{
    const QString data = imageData(object.value(QLatin1String("coverImage")).toString());
    object.remove(QLatin1String("coverImage"));
    if (!data.isEmpty()) {
        object.insert(QLatin1String("coverImageData"), data);
    }
    return object;
}

QJsonObject importCover(QJsonObject object)
{
    const QByteArray data = QByteArray::fromBase64(object.value(QLatin1String("coverImageData")).toString().toLatin1());
    object.remove(QLatin1String("coverImageData"));
    if (!data.isEmpty()) {
        QString error;
        const QString stored = ShelfCovers::storeImage(data, &error);
        if (!stored.isEmpty()) {
            object.insert(QLatin1String("coverImage"), stored);
        }
    }
    return object;
}
}

bool setBookFields(ShelfDatabase *database,
                   const QList<qint64> &bookIds,
                   Layer layer,
                   const BookMetadata &values,
                   const QStringList &fields,
                   const QStringList &cleared,
                   QString *errorString)
{
    BookMetadata source = values;
    if (fields.contains(QLatin1String("cover")) && !keepCover(&source.coverImage, errorString)) {
        return false;
    }
    database->transaction();
    for (const qint64 id : bookIds) {
        auto book = database->book(id);
        if (!book) {
            *errorString = i18n("no book %1", id);
            database->commit();
            return false;
        }
        BookMetadata &target = layer == Layer::Manual ? book->manual : book->online;
        for (const QString &field : fields) {
            ShelfMetadata::copyField(&target, source, field);
        }
        for (const QString &field : cleared) {
            ShelfMetadata::copyField(&target, BookMetadata(), field);
        }
        if (!database->saveBook(*book)) {
            *errorString = i18n("cannot save the book %1", id);
            database->commit();
            return false;
        }
        ShelfCovers::forget(id);
    }
    return database->commit();
}

bool setSeriesFields(ShelfDatabase *database,
                     qint64 shelfId,
                     const QString &key,
                     Layer layer,
                     const SeriesMetadata &values,
                     const QStringList &fields,
                     const QStringList &cleared,
                     QString *errorString)
{
    SeriesMetadata source = values;
    if (fields.contains(QLatin1String("cover")) && !keepCover(&source.coverImage, errorString)) {
        return false;
    }
    SeriesEdit edit = database->seriesEdit(shelfId, key);
    SeriesMetadata &target = layer == Layer::Manual ? edit.manual : edit.online;
    for (const QString &field : fields) {
        ShelfMetadata::copyField(&target, source, field);
    }
    for (const QString &field : cleared) {
        ShelfMetadata::copyField(&target, SeriesMetadata(), field);
    }
    if (!database->saveSeriesEdit(edit)) {
        *errorString = i18n("cannot save the series");
        return false;
    }
    return true;
}

bool exportEdits(const ShelfDatabase &database, const QString &filePath, QString *errorString)
{
    QJsonArray books;
    QJsonArray series;
    for (const Shelf &shelf : database.shelves()) {
        for (const ShelfBook &book : database.books(shelf.id)) {
            if (book.manual == BookMetadata() && book.online == BookMetadata()) {
                continue;
            }
            books.append(QJsonObject{{QLatin1String("shelf"), shelf.name},
                                     {QLatin1String("fingerprint"), book.fingerprint},
                                     {QLatin1String("path"), book.path},
                                     {QLatin1String("manual"), exportCover(ShelfMetadata::toJson(book.manual))},
                                     {QLatin1String("online"), exportCover(ShelfMetadata::toJson(book.online))}});
        }
        for (const SeriesEdit &edit : database.seriesEdits(shelf.id)) {
            series.append(QJsonObject{{QLatin1String("shelf"), shelf.name},
                                      {QLatin1String("key"), edit.key},
                                      {QLatin1String("manual"), exportCover(ShelfMetadata::toJson(edit.manual))},
                                      {QLatin1String("online"), exportCover(ShelfMetadata::toJson(edit.online))}});
        }
    }
    const QJsonObject root{{QLatin1String("format"), QStringLiteral("kobold-shelf-edits")},
                           {QLatin1String("version"), 1},
                           {QLatin1String("books"), books},
                           {QLatin1String("series"), series}};
    return writeJsonObjectFile(filePath, root, errorString);
}

ImportReport importEdits(ShelfDatabase *database, const QString &filePath)
{
    ImportReport report;
    const JsonFileReadResult file = readJsonObjectFile(filePath);
    if (file.status != JsonFileReadResult::Status::Ok) {
        report.errorString = file.status == JsonFileReadResult::Status::Missing ? i18n("%1 not found", filePath)
                                                                                : i18n("%1 is not a Kobold shelf edits file", filePath);
        return report;
    }
    if (file.object.value(QLatin1String("format")).toString() != QLatin1String("kobold-shelf-edits") || file.object.value(QLatin1String("version")).toInt() != 1) {
        report.errorString = i18n("%1 is not a Kobold shelf edits file", filePath);
        return report;
    }
    QHash<QString, Shelf> shelves;
    for (const Shelf &shelf : database->shelves()) {
        shelves.insert(shelf.name.toCaseFolded(), shelf);
    }

    database->transaction();
    for (const QJsonValue &value : file.object.value(QLatin1String("books")).toArray()) {
        const QJsonObject object = value.toObject();
        const auto shelf = shelves.constFind(object.value(QLatin1String("shelf")).toString().toCaseFolded());
        if (shelf == shelves.cend()) {
            ++report.unmatched;
            continue;
        }
        const QString fingerprint = object.value(QLatin1String("fingerprint")).toString();
        const QString path = object.value(QLatin1String("path")).toString();
        const QList<ShelfBook> books = database->books(shelf->id);
        auto match = std::find_if(books.cbegin(), books.cend(), [&fingerprint](const ShelfBook &book) {
            return !fingerprint.isEmpty() && book.fingerprint == fingerprint;
        });
        if (match == books.cend()) {
            match = std::find_if(books.cbegin(), books.cend(), [&path](const ShelfBook &book) {
                return book.path == path;
            });
        }
        if (match == books.cend()) {
            ++report.unmatched;
            continue;
        }
        ShelfBook book = *match;
        book.manual = ShelfMetadata::bookFromJson(importCover(object.value(QLatin1String("manual")).toObject()));
        book.online = ShelfMetadata::bookFromJson(importCover(object.value(QLatin1String("online")).toObject()));
        database->saveBook(book);
        ShelfCovers::forget(book.id);
        ++report.books;
    }
    for (const QJsonValue &value : file.object.value(QLatin1String("series")).toArray()) {
        const QJsonObject object = value.toObject();
        const auto shelf = shelves.constFind(object.value(QLatin1String("shelf")).toString().toCaseFolded());
        if (shelf == shelves.cend()) {
            continue;
        }
        database->saveSeriesEdit(SeriesEdit{shelf->id,
                                            object.value(QLatin1String("key")).toString(),
                                            ShelfMetadata::seriesFromJson(importCover(object.value(QLatin1String("manual")).toObject())),
                                            ShelfMetadata::seriesFromJson(importCover(object.value(QLatin1String("online")).toObject()))});
        ++report.series;
    }
    report.ok = database->commit();
    if (!report.ok) {
        report.errorString = i18n("cannot save the imported values");
    }
    return report;
}

} // namespace Kobold::ShelfEdits
