// SPDX-License-Identifier: MIT

#include "shelfmetadata.h"

#include <KZip>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextDocumentFragment>
#include <QUrl>
#include <QXmlStreamReader>

#ifdef KOBOLD_HAVE_POPPLER
#include <poppler-qt6.h>
#endif

#include <algorithm>
#include <memory>

namespace Kobold::ShelfMetadata
{

namespace
{
const QStringList ImageSuffixes{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("gif")};

QStringList splitList(const QString &text)
{
    QStringList items;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        const QString item = part.trimmed();
        if (!item.isEmpty() && !items.contains(item, Qt::CaseInsensitive)) {
            items.append(item);
        }
    }
    return items;
}

std::optional<double> parseNumber(const QString &text)
{
    bool ok = false;
    const double value = QString(text).replace(QLatin1Char(','), QLatin1Char('.')).trimmed().toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

QDate parseDate(const QString &text)
{
    const QString trimmed = text.trimmed();
    for (const QString &format : {QStringLiteral("yyyy-MM-dd"), QStringLiteral("yyyy-MM"), QStringLiteral("yyyy")}) {
        const QDate date = QDate::fromString(trimmed.left(format.size()), format);
        if (date.isValid()) {
            return date;
        }
    }
    return {};
}

QString plainText(const QString &html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

QString isbnIn(const QString &text)
{
    QString digits = text;
    digits.remove(QRegularExpression(QStringLiteral("[^0-9Xx]")));
    return digits.size() == 10 || digits.size() == 13 ? digits.toUpper() : QString();
}

std::unique_ptr<KZip> openZip(const QString &path)
{
    auto zip = std::make_unique<KZip>(path);
    if (!zip->open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    return zip;
}

QByteArray zipFile(const KZip &zip, const QString &name)
{
    const KArchiveFile *file = zip.directory()->file(name);
    return file ? file->data() : QByteArray();
}

// Every file path of an archive directory, recursively.
void listFiles(const KArchiveDirectory *directory, const QString &prefix, QStringList *paths)
{
    const QStringList entries = directory->entries();
    for (const QString &entry : entries) {
        const KArchiveEntry *item = directory->entry(entry);
        if (item->isDirectory()) {
            listFiles(static_cast<const KArchiveDirectory *>(item), prefix + entry + QLatin1Char('/'), paths);
        } else {
            paths->append(prefix + entry);
        }
    }
}

bool isImage(const QString &path)
{
    return ImageSuffixes.contains(QFileInfo(path).suffix().toLower()) && !QFileInfo(path).fileName().startsWith(QLatin1Char('.'));
}

// Natural order, "page2" before "page10", ignoring case. QCollator's numeric mode needs a real
// locale and falls back to plain order under C/POSIX (containers, minimal systems).
bool naturalLess(const QString &a, const QString &b)
{
    qsizetype i = 0;
    qsizetype j = 0;
    while (i < a.size() && j < b.size()) {
        if (a.at(i).isDigit() && b.at(j).isDigit()) {
            qsizetype endA = i;
            qsizetype endB = j;
            while (endA < a.size() && a.at(endA).isDigit()) {
                ++endA;
            }
            while (endB < b.size() && b.at(endB).isDigit()) {
                ++endB;
            }
            // Compare the numbers without converting them: no overflow on long runs.
            QStringView numberA = QStringView(a).mid(i, endA - i);
            QStringView numberB = QStringView(b).mid(j, endB - j);
            while (numberA.size() > 1 && numberA.front() == QLatin1Char('0')) {
                numberA = numberA.mid(1);
            }
            while (numberB.size() > 1 && numberB.front() == QLatin1Char('0')) {
                numberB = numberB.mid(1);
            }
            if (numberA.size() != numberB.size()) {
                return numberA.size() < numberB.size();
            }
            if (const int order = numberA.compare(numberB); order != 0) {
                return order < 0;
            }
            i = endA;
            j = endB;
            continue;
        }
        const QChar ca = a.at(i).toCaseFolded();
        const QChar cb = b.at(j).toCaseFolded();
        if (ca != cb) {
            return ca < cb;
        }
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

// The first image in natural name order: the cover of a comic.
QString firstImage(QStringList paths)
{
    paths.removeIf([](const QString &path) {
        return !isImage(path) || path.startsWith(QLatin1String("__MACOSX"));
    });
    std::sort(paths.begin(), paths.end(), naturalLess);
    return paths.value(0);
}

// The OPF path from META-INF/container.xml.
QString opfPath(const KZip &zip)
{
    QXmlStreamReader xml(zipFile(zip, QStringLiteral("META-INF/container.xml")));
    while (xml.readNextStartElement() || !xml.atEnd()) {
        if (xml.isStartElement() && xml.name() == QLatin1String("rootfile")) {
            return xml.attributes().value(QLatin1String("full-path")).toString();
        }
        if (xml.hasError()) {
            break;
        }
        if (!xml.isStartElement()) {
            xml.readNext();
        }
    }
    return {};
}

// "7z" and its arguments; CBR archives are read with it (SPEC-SHELF.md §8).
QByteArray sevenZip(const QStringList &arguments)
{
    const QString program = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (program.isEmpty()) {
        return {};
    }
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(60000) || process.exitCode() != 0) {
        return {};
    }
    return process.readAllStandardOutput();
}

QStringList sevenZipList(const QString &path)
{
    QStringList paths;
    const QList<QByteArray> lines = sevenZip({QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("-ba"), path}).split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("Path = ")) {
            paths.append(QString::fromUtf8(line.mid(7)).trimmed());
        }
    }
    return paths;
}

QByteArray sevenZipFile(const QString &path, const QString &name)
{
    return sevenZip({QStringLiteral("e"), QStringLiteral("-so"), path, name});
}
}

BookMetadata merge(const BookMetadata &primary, const BookMetadata &fallback)
{
    BookMetadata merged = primary;
    const auto fill = [](auto &target, const auto &source) {
        if (target.isEmpty()) {
            target = source;
        }
    };
    fill(merged.title, fallback.title);
    fill(merged.authors, fallback.authors);
    fill(merged.series, fallback.series);
    if (!merged.number) {
        merged.number = fallback.number;
    }
    fill(merged.summary, fallback.summary);
    fill(merged.publisher, fallback.publisher);
    fill(merged.language, fallback.language);
    fill(merged.genres, fallback.genres);
    fill(merged.tags, fallback.tags);
    if (merged.ageRating < 0) {
        merged.ageRating = fallback.ageRating;
    }
    if (!merged.releaseDate.isValid()) {
        merged.releaseDate = fallback.releaseDate;
    }
    fill(merged.isbn, fallback.isbn);
    fill(merged.readingDirection, fallback.readingDirection);
    fill(merged.sortTitle, fallback.sortTitle);
    // A chosen cover replaces the file's as a whole: an image or a page, not both.
    if (merged.coverImage.isEmpty() && merged.coverPage < 0) {
        merged.coverImage = fallback.coverImage;
        merged.coverPage = fallback.coverPage;
    }
    return merged;
}

SeriesMetadata merge(const SeriesMetadata &primary, const SeriesMetadata &fallback)
{
    SeriesMetadata merged = primary;
    const auto fill = [](auto &target, const auto &source) {
        if (target.isEmpty()) {
            target = source;
        }
    };
    fill(merged.name, fallback.name);
    fill(merged.sortName, fallback.sortName);
    fill(merged.summary, fallback.summary);
    fill(merged.genres, fallback.genres);
    if (merged.ageRating < 0) {
        merged.ageRating = fallback.ageRating;
    }
    fill(merged.readingDirection, fallback.readingDirection);
    fill(merged.coverImage, fallback.coverImage);
    return merged;
}

QStringList bookFields()
{
    return {QStringLiteral("title"),
            QStringLiteral("sortTitle"),
            QStringLiteral("authors"),
            QStringLiteral("series"),
            QStringLiteral("number"),
            QStringLiteral("summary"),
            QStringLiteral("publisher"),
            QStringLiteral("language"),
            QStringLiteral("genres"),
            QStringLiteral("tags"),
            QStringLiteral("ageRating"),
            QStringLiteral("releaseDate"),
            QStringLiteral("isbn"),
            QStringLiteral("readingDirection"),
            QStringLiteral("cover")};
}

QStringList seriesFields()
{
    return {QStringLiteral("name"),
            QStringLiteral("sortName"),
            QStringLiteral("summary"),
            QStringLiteral("genres"),
            QStringLiteral("ageRating"),
            QStringLiteral("readingDirection"),
            QStringLiteral("cover")};
}

namespace
{
bool parseDirection(const QString &text, QString *direction)
{
    const QString value = text.trimmed().toLower();
    if (value.isEmpty() || value == QLatin1String("ltr")) {
        *direction = QString();
        return true;
    }
    if (value == QLatin1String("rtl")) {
        *direction = QStringLiteral("rtl");
        return true;
    }
    return false;
}

bool parseAge(const QString &text, int *age)
{
    if (text.trimmed().isEmpty()) {
        *age = -1;
        return true;
    }
    bool ok = false;
    const int value = text.trimmed().toInt(&ok);
    if (!ok || value < 0 || value > 99) {
        return false;
    }
    *age = value;
    return true;
}
}

bool hasField(const BookMetadata &metadata, const QString &field)
{
    if (field == QLatin1String("number")) {
        return metadata.number.has_value();
    }
    if (field == QLatin1String("ageRating")) {
        return metadata.ageRating >= 0;
    }
    if (field == QLatin1String("releaseDate")) {
        return metadata.releaseDate.isValid();
    }
    if (field == QLatin1String("cover")) {
        return !metadata.coverImage.isEmpty() || metadata.coverPage >= 0;
    }
    return !fieldText(metadata, field).isEmpty();
}

bool hasField(const SeriesMetadata &metadata, const QString &field)
{
    if (field == QLatin1String("ageRating")) {
        return metadata.ageRating >= 0;
    }
    return !fieldText(metadata, field).isEmpty();
}

void copyField(BookMetadata *target, const BookMetadata &source, const QString &field)
{
    if (field == QLatin1String("title")) {
        target->title = source.title;
    } else if (field == QLatin1String("sortTitle")) {
        target->sortTitle = source.sortTitle;
    } else if (field == QLatin1String("authors")) {
        target->authors = source.authors;
    } else if (field == QLatin1String("series")) {
        target->series = source.series;
    } else if (field == QLatin1String("number")) {
        target->number = source.number;
    } else if (field == QLatin1String("summary")) {
        target->summary = source.summary;
    } else if (field == QLatin1String("publisher")) {
        target->publisher = source.publisher;
    } else if (field == QLatin1String("language")) {
        target->language = source.language;
    } else if (field == QLatin1String("genres")) {
        target->genres = source.genres;
    } else if (field == QLatin1String("tags")) {
        target->tags = source.tags;
    } else if (field == QLatin1String("ageRating")) {
        target->ageRating = source.ageRating;
    } else if (field == QLatin1String("releaseDate")) {
        target->releaseDate = source.releaseDate;
    } else if (field == QLatin1String("isbn")) {
        target->isbn = source.isbn;
    } else if (field == QLatin1String("readingDirection")) {
        target->readingDirection = source.readingDirection;
    } else if (field == QLatin1String("cover")) {
        target->coverImage = source.coverImage;
        target->coverPage = source.coverPage;
    }
}

void copyField(SeriesMetadata *target, const SeriesMetadata &source, const QString &field)
{
    if (field == QLatin1String("name")) {
        target->name = source.name;
    } else if (field == QLatin1String("sortName")) {
        target->sortName = source.sortName;
    } else if (field == QLatin1String("summary")) {
        target->summary = source.summary;
    } else if (field == QLatin1String("genres")) {
        target->genres = source.genres;
    } else if (field == QLatin1String("ageRating")) {
        target->ageRating = source.ageRating;
    } else if (field == QLatin1String("readingDirection")) {
        target->readingDirection = source.readingDirection;
    } else if (field == QLatin1String("cover")) {
        target->coverImage = source.coverImage;
    }
}

QString fieldText(const BookMetadata &metadata, const QString &field)
{
    if (field == QLatin1String("title")) {
        return metadata.title;
    }
    if (field == QLatin1String("sortTitle")) {
        return metadata.sortTitle;
    }
    if (field == QLatin1String("authors")) {
        return metadata.authors.join(QLatin1String(", "));
    }
    if (field == QLatin1String("series")) {
        return metadata.series;
    }
    if (field == QLatin1String("number")) {
        return metadata.number ? QString::number(*metadata.number) : QString();
    }
    if (field == QLatin1String("summary")) {
        return metadata.summary;
    }
    if (field == QLatin1String("publisher")) {
        return metadata.publisher;
    }
    if (field == QLatin1String("language")) {
        return metadata.language;
    }
    if (field == QLatin1String("genres")) {
        return metadata.genres.join(QLatin1String(", "));
    }
    if (field == QLatin1String("tags")) {
        return metadata.tags.join(QLatin1String(", "));
    }
    if (field == QLatin1String("ageRating")) {
        return metadata.ageRating >= 0 ? QString::number(metadata.ageRating) : QString();
    }
    if (field == QLatin1String("releaseDate")) {
        return metadata.releaseDate.isValid() ? metadata.releaseDate.toString(Qt::ISODate) : QString();
    }
    if (field == QLatin1String("isbn")) {
        return metadata.isbn;
    }
    if (field == QLatin1String("readingDirection")) {
        return metadata.readingDirection;
    }
    if (field == QLatin1String("cover")) {
        return !metadata.coverImage.isEmpty() ? metadata.coverImage : metadata.coverPage >= 0 ? QStringLiteral("page:%1").arg(metadata.coverPage + 1) : QString();
    }
    return {};
}

QString fieldText(const SeriesMetadata &metadata, const QString &field)
{
    if (field == QLatin1String("name")) {
        return metadata.name;
    }
    if (field == QLatin1String("sortName")) {
        return metadata.sortName;
    }
    if (field == QLatin1String("summary")) {
        return metadata.summary;
    }
    if (field == QLatin1String("genres")) {
        return metadata.genres.join(QLatin1String(", "));
    }
    if (field == QLatin1String("ageRating")) {
        return metadata.ageRating >= 0 ? QString::number(metadata.ageRating) : QString();
    }
    if (field == QLatin1String("readingDirection")) {
        return metadata.readingDirection;
    }
    if (field == QLatin1String("cover")) {
        return metadata.coverImage;
    }
    return {};
}

bool setFieldText(BookMetadata *metadata, const QString &field, const QString &text)
{
    const QString value = text.trimmed();
    if (field == QLatin1String("title")) {
        metadata->title = value;
    } else if (field == QLatin1String("sortTitle")) {
        metadata->sortTitle = value;
    } else if (field == QLatin1String("authors")) {
        metadata->authors = splitList(value);
    } else if (field == QLatin1String("series")) {
        metadata->series = value;
    } else if (field == QLatin1String("number")) {
        if (value.isEmpty()) {
            metadata->number.reset();
            return true;
        }
        bool ok = false;
        const double number = QLocale::c().toDouble(QString(value).replace(QLatin1Char(','), QLatin1Char('.')), &ok);
        if (!ok) {
            return false;
        }
        metadata->number = number;
    } else if (field == QLatin1String("summary")) {
        metadata->summary = value;
    } else if (field == QLatin1String("publisher")) {
        metadata->publisher = value;
    } else if (field == QLatin1String("language")) {
        metadata->language = value;
    } else if (field == QLatin1String("genres")) {
        metadata->genres = splitList(value);
    } else if (field == QLatin1String("tags")) {
        metadata->tags = splitList(value);
    } else if (field == QLatin1String("ageRating")) {
        return parseAge(value, &metadata->ageRating);
    } else if (field == QLatin1String("releaseDate")) {
        if (value.isEmpty()) {
            metadata->releaseDate = QDate();
            return true;
        }
        QDate date = QDate::fromString(value, Qt::ISODate);
        if (!date.isValid() && value.size() == 4) {
            date = QDate(value.toInt(), 1, 1);
        }
        if (!date.isValid()) {
            return false;
        }
        metadata->releaseDate = date;
    } else if (field == QLatin1String("isbn")) {
        metadata->isbn = QString(value).remove(QLatin1Char('-')).remove(QLatin1Char(' '));
    } else if (field == QLatin1String("readingDirection")) {
        return parseDirection(value, &metadata->readingDirection);
    } else if (field == QLatin1String("cover")) {
        metadata->coverImage.clear();
        metadata->coverPage = -1;
        if (value.startsWith(QLatin1String("page:"))) {
            bool ok = false;
            const int page = value.mid(5).toInt(&ok);
            if (!ok || page < 1) {
                return false;
            }
            metadata->coverPage = page - 1;
        } else {
            metadata->coverImage = value;
        }
    } else {
        return false;
    }
    return true;
}

bool setFieldText(SeriesMetadata *metadata, const QString &field, const QString &text)
{
    const QString value = text.trimmed();
    if (field == QLatin1String("name")) {
        metadata->name = value;
    } else if (field == QLatin1String("sortName")) {
        metadata->sortName = value;
    } else if (field == QLatin1String("summary")) {
        metadata->summary = value;
    } else if (field == QLatin1String("genres")) {
        metadata->genres = splitList(value);
    } else if (field == QLatin1String("ageRating")) {
        return parseAge(value, &metadata->ageRating);
    } else if (field == QLatin1String("readingDirection")) {
        return parseDirection(value, &metadata->readingDirection);
    } else if (field == QLatin1String("cover")) {
        metadata->coverImage = value;
    } else {
        return false;
    }
    return true;
}

QJsonObject toJson(const BookMetadata &metadata)
{
    QJsonObject object;
    const auto put = [&object](const char *key, const QString &value) {
        if (!value.isEmpty()) {
            object.insert(QLatin1String(key), value);
        }
    };
    const auto putList = [&object](const char *key, const QStringList &value) {
        if (!value.isEmpty()) {
            object.insert(QLatin1String(key), QJsonArray::fromStringList(value));
        }
    };
    put("title", metadata.title);
    put("sortTitle", metadata.sortTitle);
    putList("authors", metadata.authors);
    put("series", metadata.series);
    if (metadata.number) {
        object.insert(QLatin1String("number"), *metadata.number);
    }
    put("summary", metadata.summary);
    put("publisher", metadata.publisher);
    put("language", metadata.language);
    putList("genres", metadata.genres);
    putList("tags", metadata.tags);
    if (metadata.ageRating >= 0) {
        object.insert(QLatin1String("ageRating"), metadata.ageRating);
    }
    if (metadata.releaseDate.isValid()) {
        object.insert(QLatin1String("releaseDate"), metadata.releaseDate.toString(Qt::ISODate));
    }
    put("isbn", metadata.isbn);
    put("readingDirection", metadata.readingDirection);
    put("coverImage", metadata.coverImage);
    if (metadata.coverPage >= 0) {
        object.insert(QLatin1String("coverPage"), metadata.coverPage);
    }
    return object;
}

namespace
{
QStringList stringList(const QJsonValue &value)
{
    QStringList list;
    const QJsonArray array = value.toArray();
    for (const QJsonValue &item : array) {
        list.append(item.toString());
    }
    return list;
}
}

BookMetadata bookFromJson(const QJsonObject &object)
{
    BookMetadata metadata;
    metadata.title = object.value(QLatin1String("title")).toString();
    metadata.sortTitle = object.value(QLatin1String("sortTitle")).toString();
    metadata.authors = stringList(object.value(QLatin1String("authors")));
    metadata.series = object.value(QLatin1String("series")).toString();
    if (object.contains(QLatin1String("number"))) {
        metadata.number = object.value(QLatin1String("number")).toDouble();
    }
    metadata.summary = object.value(QLatin1String("summary")).toString();
    metadata.publisher = object.value(QLatin1String("publisher")).toString();
    metadata.language = object.value(QLatin1String("language")).toString();
    metadata.genres = stringList(object.value(QLatin1String("genres")));
    metadata.tags = stringList(object.value(QLatin1String("tags")));
    metadata.ageRating = object.value(QLatin1String("ageRating")).toInt(-1);
    metadata.releaseDate = QDate::fromString(object.value(QLatin1String("releaseDate")).toString(), Qt::ISODate);
    metadata.isbn = object.value(QLatin1String("isbn")).toString();
    metadata.readingDirection = object.value(QLatin1String("readingDirection")).toString();
    metadata.coverImage = object.value(QLatin1String("coverImage")).toString();
    metadata.coverPage = object.value(QLatin1String("coverPage")).toInt(-1);
    return metadata;
}

QJsonObject toJson(const SeriesMetadata &metadata)
{
    QJsonObject object;
    const auto put = [&object](const char *key, const QString &value) {
        if (!value.isEmpty()) {
            object.insert(QLatin1String(key), value);
        }
    };
    put("name", metadata.name);
    put("sortName", metadata.sortName);
    put("summary", metadata.summary);
    if (!metadata.genres.isEmpty()) {
        object.insert(QLatin1String("genres"), QJsonArray::fromStringList(metadata.genres));
    }
    if (metadata.ageRating >= 0) {
        object.insert(QLatin1String("ageRating"), metadata.ageRating);
    }
    put("readingDirection", metadata.readingDirection);
    put("coverImage", metadata.coverImage);
    return object;
}

SeriesMetadata seriesFromJson(const QJsonObject &object)
{
    SeriesMetadata metadata;
    metadata.name = object.value(QLatin1String("name")).toString();
    metadata.sortName = object.value(QLatin1String("sortName")).toString();
    metadata.summary = object.value(QLatin1String("summary")).toString();
    metadata.genres = stringList(object.value(QLatin1String("genres")));
    metadata.ageRating = object.value(QLatin1String("ageRating")).toInt(-1);
    metadata.readingDirection = object.value(QLatin1String("readingDirection")).toString();
    metadata.coverImage = object.value(QLatin1String("coverImage")).toString();
    return metadata;
}

BookMetadata parseOpf(const QByteArray &opf)
{
    BookMetadata metadata;
    QHash<QString, QString> collections; // EPUB 3 collection id → name
    QHash<QString, QString> positions;   // collection id → group-position
    QHash<QString, QString> roles;       // creator id → role (EPUB 3 refines)
    QList<std::pair<QString, QString>> creators; // id, name
    QString calibreIndex;

    QXmlStreamReader xml(opf);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        const QStringView name = xml.name();
        const QXmlStreamAttributes attributes = xml.attributes();
        if (name == QLatin1String("title") && metadata.title.isEmpty()) {
            metadata.title = xml.readElementText().trimmed();
        } else if (name == QLatin1String("creator")) {
            const QString role = attributes.value(QLatin1String("opf:role")).toString().isEmpty() ? attributes.value(QLatin1String("role")).toString()
                                                                                                   : attributes.value(QLatin1String("opf:role")).toString();
            const QString creator = xml.readElementText().trimmed();
            // EPUB 3 refines roles by id; EPUB 2 puts the role on the element.
            const QString key = attributes.value(QLatin1String("id")).toString().isEmpty() ? creator : attributes.value(QLatin1String("id")).toString();
            if (!role.isEmpty()) {
                roles.insert(key, role);
            }
            creators.append({key, creator});
        } else if (name == QLatin1String("publisher")) {
            metadata.publisher = xml.readElementText().trimmed();
        } else if (name == QLatin1String("language") && metadata.language.isEmpty()) {
            metadata.language = xml.readElementText().trimmed();
        } else if (name == QLatin1String("subject")) {
            const QString subject = xml.readElementText().trimmed();
            if (!subject.isEmpty() && !metadata.genres.contains(subject, Qt::CaseInsensitive)) {
                metadata.genres.append(subject);
            }
        } else if (name == QLatin1String("description")) {
            metadata.summary = plainText(xml.readElementText());
        } else if (name == QLatin1String("date") && !metadata.releaseDate.isValid()) {
            metadata.releaseDate = parseDate(xml.readElementText());
        } else if (name == QLatin1String("identifier")) {
            const QString scheme = attributes.value(QLatin1String("opf:scheme")).toString();
            const QString value = xml.readElementText();
            if (metadata.isbn.isEmpty() && (scheme.compare(QLatin1String("ISBN"), Qt::CaseInsensitive) == 0 || value.contains(QLatin1String("isbn"), Qt::CaseInsensitive))) {
                metadata.isbn = isbnIn(value);
            }
        } else if (name == QLatin1String("meta")) {
            const QString metaName = attributes.value(QLatin1String("name")).toString();
            const QString property = attributes.value(QLatin1String("property")).toString();
            const QString refines = attributes.value(QLatin1String("refines")).toString().remove(QLatin1Char('#'));
            if (metaName == QLatin1String("calibre:series")) {
                metadata.series = attributes.value(QLatin1String("content")).toString().trimmed();
            } else if (metaName == QLatin1String("calibre:series_index")) {
                calibreIndex = attributes.value(QLatin1String("content")).toString();
            } else if (property == QLatin1String("belongs-to-collection")) {
                collections.insert(attributes.value(QLatin1String("id")).toString(), xml.readElementText().trimmed());
            } else if (property == QLatin1String("group-position")) {
                positions.insert(refines, xml.readElementText());
            } else if (property == QLatin1String("role")) {
                roles.insert(refines, xml.readElementText().trimmed());
            }
        }
    }

    for (const auto &[id, creator] : std::as_const(creators)) {
        const QString role = roles.value(id);
        if ((role.isEmpty() || role == QLatin1String("aut")) && !creator.isEmpty()) {
            metadata.authors.append(creator);
        }
    }
    if (metadata.series.isEmpty() && !collections.isEmpty()) {
        const QString id = collections.keys().constFirst();
        metadata.series = collections.value(id);
        calibreIndex = positions.value(id);
    }
    if (!metadata.series.isEmpty()) {
        metadata.number = parseNumber(calibreIndex);
    }
    return metadata;
}

int ageFromRating(const QString &rating)
{
    const QString text = rating.trimmed().toLower();
    if (text.isEmpty() || text.contains(QLatin1String("unknown")) || text.contains(QLatin1String("pending"))) {
        return -1;
    }
    static const QRegularExpression digits(QStringLiteral("(\\d+)\\s*\\+"));
    if (const auto match = digits.match(text); match.hasMatch()) {
        return match.captured(1).toInt();
    }
    if (text.contains(QLatin1String("adult")) || text == QLatin1String("x") || text.startsWith(QLatin1String("r18")) || text.startsWith(QLatin1String("x18"))) {
        return 18;
    }
    if (text.startsWith(QLatin1String("mature")) || text == QLatin1String("m")) {
        return 17;
    }
    if (text.startsWith(QLatin1String("teen"))) {
        return 13;
    }
    if (text.contains(QLatin1String("kids to adults"))) {
        return 6;
    }
    if (text.contains(QLatin1String("early childhood"))) {
        return 3;
    }
    if (text.startsWith(QLatin1String("everyone")) || text == QLatin1String("g")) {
        return 0;
    }
    return -1;
}

BookMetadata parseComicInfo(const QByteArray &xmlData)
{
    BookMetadata metadata;
    QString year;
    QString month;
    QString day;
    QXmlStreamReader xml(xmlData);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement() || xml.name() == QLatin1String("ComicInfo")) {
            continue;
        }
        const QString name = xml.name().toString();
        const QString value = xml.readElementText().trimmed();
        if (name == QLatin1String("Series")) {
            metadata.series = value;
        } else if (name == QLatin1String("Number")) {
            metadata.number = parseNumber(value);
        } else if (name == QLatin1String("Title")) {
            metadata.title = value;
        } else if (name == QLatin1String("Writer")) {
            metadata.authors = splitList(value);
        } else if (name == QLatin1String("Publisher")) {
            metadata.publisher = value;
        } else if (name == QLatin1String("Genre")) {
            metadata.genres = splitList(value);
        } else if (name == QLatin1String("Tags")) {
            metadata.tags = splitList(value);
        } else if (name == QLatin1String("Summary")) {
            metadata.summary = value;
        } else if (name == QLatin1String("AgeRating")) {
            metadata.ageRating = ageFromRating(value);
        } else if (name == QLatin1String("LanguageISO")) {
            metadata.language = value;
        } else if (name == QLatin1String("Year")) {
            year = value;
        } else if (name == QLatin1String("Month")) {
            month = value;
        } else if (name == QLatin1String("Day")) {
            day = value;
        } else if (name == QLatin1String("Manga") && value == QLatin1String("YesAndRightToLeft")) {
            metadata.readingDirection = QStringLiteral("rtl");
        }
    }
    if (!year.isEmpty()) {
        metadata.releaseDate = QDate(year.toInt(), month.isEmpty() ? 1 : month.toInt(), day.isEmpty() ? 1 : day.toInt());
    }
    return metadata;
}

std::optional<double> volumeInName(const QString &baseName)
{
    static const QList<QRegularExpression> patterns{
        QRegularExpression(QStringLiteral("\\b(?:tome|vol(?:ume)?\\.?|t|v|n°|no\\.?)\\s*(\\d+(?:[.,]\\d+)?)\\b"), QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(QStringLiteral("#\\s*(\\d+(?:[.,]\\d+)?)")),
        QRegularExpression(QStringLiteral("\\s-\\s*(\\d+(?:[.,]\\d+)?)\\b")),
        QRegularExpression(QStringLiteral("(\\d+(?:[.,]\\d+)?)\\s*$")),
    };
    for (const QRegularExpression &pattern : patterns) {
        if (const auto match = pattern.match(baseName); match.hasMatch()) {
            return parseNumber(match.captured(1));
        }
    }
    return std::nullopt;
}

BookMetadata fromNames(const QString &relativePath)
{
    BookMetadata metadata;
    const QFileInfo info(relativePath);
    QString base = info.fileName();
    for (const QLatin1StringView extension : {QLatin1StringView(".kepub.epub"),
                                              QLatin1StringView(".epub"),
                                              QLatin1StringView(".pdf"),
                                              QLatin1StringView(".cbz"),
                                              QLatin1StringView(".cbr")}) {
        if (base.endsWith(extension, Qt::CaseInsensitive)) {
            base.chop(extension.size());
            break;
        }
    }
    base.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!base.contains(QLatin1Char(' ')) && base.count(QLatin1Char('.')) >= 2) {
        base.replace(QLatin1Char('.'), QLatin1Char(' '));
    }
    base = base.simplified();
    metadata.title = base;
    metadata.number = volumeInName(base);

    const QString folder = QFileInfo(info.path()).fileName();
    if (info.path() != QLatin1String(".") && !folder.isEmpty()) {
        metadata.series = folder;
    } else if (metadata.number) {
        // At the shelf's root: the series is the name without its volume part.
        static const QRegularExpression volumePart(
            QStringLiteral("\\s*[-–]?\\s*(?:tome|vol(?:ume)?\\.?|t|v|n°|no\\.?|#)?\\s*\\d+(?:[.,]\\d+)?\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        metadata.series = QString(base).remove(volumePart).trimmed();
    }
    return metadata;
}

BookMetadata readEmbedded(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QLatin1String(".epub"))) {
        const auto zip = openZip(path);
        if (!zip) {
            return {};
        }
        return parseOpf(zipFile(*zip, opfPath(*zip)));
    }
    if (lower.endsWith(QLatin1String(".cbz"))) {
        const auto zip = openZip(path);
        if (!zip) {
            return {};
        }
        QStringList files;
        listFiles(zip->directory(), QString(), &files);
        for (const QString &file : std::as_const(files)) {
            if (QFileInfo(file).fileName().compare(QLatin1String("ComicInfo.xml"), Qt::CaseInsensitive) == 0) {
                return parseComicInfo(zipFile(*zip, file));
            }
        }
        return {};
    }
    if (lower.endsWith(QLatin1String(".cbr"))) {
        for (const QString &file : sevenZipList(path)) {
            if (QFileInfo(file).fileName().compare(QLatin1String("ComicInfo.xml"), Qt::CaseInsensitive) == 0) {
                return parseComicInfo(sevenZipFile(path, file));
            }
        }
        return {};
    }
#ifdef KOBOLD_HAVE_POPPLER
    if (lower.endsWith(QLatin1String(".pdf"))) {
        const std::unique_ptr<Poppler::Document> document = Poppler::Document::load(path);
        if (!document || document->isLocked()) {
            return {};
        }
        BookMetadata metadata;
        metadata.title = document->info(QStringLiteral("Title")).trimmed();
        metadata.authors = splitList(document->info(QStringLiteral("Author")));
        metadata.summary = document->info(QStringLiteral("Subject")).trimmed();
        metadata.tags = splitList(document->info(QStringLiteral("Keywords")));
        return metadata;
    }
#endif
    return {};
}

namespace
{
// Images of a comic in reading order.
QStringList comicPages(const QString &path, const KZip *zip)
{
    QStringList files;
    if (zip) {
        listFiles(zip->directory(), QString(), &files);
    } else {
        files = sevenZipList(path);
    }
    files.removeIf([](const QString &file) {
        return !isImage(file) || file.startsWith(QLatin1String("__MACOSX"));
    });
    std::sort(files.begin(), files.end(), naturalLess);
    return files;
}
}

QImage readPage(const QString &path, int index)
{
    const QString lower = path.toLower();
    if (index < 0) {
        return {};
    }
    if (lower.endsWith(QLatin1String(".cbz"))) {
        const auto zip = openZip(path);
        if (!zip) {
            return {};
        }
        const QString page = comicPages(path, zip.get()).value(index);
        return page.isEmpty() ? QImage() : QImage::fromData(zipFile(*zip, page));
    }
    if (lower.endsWith(QLatin1String(".cbr"))) {
        const QString page = comicPages(path, nullptr).value(index);
        return page.isEmpty() ? QImage() : QImage::fromData(sevenZipFile(path, page));
    }
#ifdef KOBOLD_HAVE_POPPLER
    if (lower.endsWith(QLatin1String(".pdf"))) {
        const std::unique_ptr<Poppler::Document> document = Poppler::Document::load(path);
        if (!document || document->isLocked() || index >= document->numPages()) {
            return {};
        }
        const std::unique_ptr<Poppler::Page> page = document->page(index);
        return page ? page->renderToImage(72.0, 72.0) : QImage();
    }
#endif
    return {};
}

int pageCount(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QLatin1String(".cbz"))) {
        const auto zip = openZip(path);
        return zip ? int(comicPages(path, zip.get()).size()) : 0;
    }
    if (lower.endsWith(QLatin1String(".cbr"))) {
        return int(comicPages(path, nullptr).size());
    }
#ifdef KOBOLD_HAVE_POPPLER
    if (lower.endsWith(QLatin1String(".pdf"))) {
        const std::unique_ptr<Poppler::Document> document = Poppler::Document::load(path);
        return document && !document->isLocked() ? document->numPages() : 0;
    }
#endif
    return 0;
}

QImage readCover(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QLatin1String(".epub"))) {
        const auto zip = openZip(path);
        if (!zip) {
            return {};
        }
        const QString opf = opfPath(*zip);
        const QString base = QFileInfo(opf).path() == QLatin1String(".") ? QString() : QFileInfo(opf).path() + QLatin1Char('/');
        // <meta name="cover" content="id"/> or an item with properties="cover-image".
        QString coverId;
        QHash<QString, QString> hrefs;
        QString coverHref;
        QXmlStreamReader xml(zipFile(*zip, opf));
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement()) {
                continue;
            }
            const QXmlStreamAttributes attributes = xml.attributes();
            if (xml.name() == QLatin1String("meta") && attributes.value(QLatin1String("name")) == QLatin1String("cover")) {
                coverId = attributes.value(QLatin1String("content")).toString();
            } else if (xml.name() == QLatin1String("item")) {
                const QString href = QUrl::fromPercentEncoding(attributes.value(QLatin1String("href")).toUtf8());
                hrefs.insert(attributes.value(QLatin1String("id")).toString(), href);
                if (attributes.value(QLatin1String("properties")).contains(QLatin1String("cover-image"))) {
                    coverHref = href;
                }
            }
        }
        if (coverHref.isEmpty()) {
            coverHref = hrefs.value(coverId);
        }
        if (coverHref.isEmpty()) {
            return {};
        }
        return QImage::fromData(zipFile(*zip, QDir::cleanPath(base + coverHref)));
    }
    if (lower.endsWith(QLatin1String(".cbz"))) {
        const auto zip = openZip(path);
        if (!zip) {
            return {};
        }
        QStringList files;
        listFiles(zip->directory(), QString(), &files);
        const QString first = firstImage(files);
        return first.isEmpty() ? QImage() : QImage::fromData(zipFile(*zip, first));
    }
    if (lower.endsWith(QLatin1String(".cbr"))) {
        const QString first = firstImage(sevenZipList(path));
        return first.isEmpty() ? QImage() : QImage::fromData(sevenZipFile(path, first));
    }
#ifdef KOBOLD_HAVE_POPPLER
    if (lower.endsWith(QLatin1String(".pdf"))) {
        const std::unique_ptr<Poppler::Document> document = Poppler::Document::load(path);
        if (!document || document->isLocked() || document->numPages() == 0) {
            return {};
        }
        const std::unique_ptr<Poppler::Page> page = document->page(0);
        return page ? page->renderToImage(72.0, 72.0) : QImage();
    }
#endif
    return {};
}

} // namespace Kobold::ShelfMetadata
