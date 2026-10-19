// SPDX-License-Identifier: MIT

#include "filenaming.h"
#include "localfiles.h"
#include "komgabook.h"

namespace Kobold::FileNaming
{

namespace
{
bool isForbidden(QChar c)
{
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    return c.unicode() < 0x20 || c.unicode() == 0x7f || forbidden.contains(c);
}

bool isTrimmed(QChar c)
{
    return c.isSpace() || c == QLatin1Char('.');
}

QString trimSpacesAndDots(const QString &string)
{
    qsizetype begin = 0;
    qsizetype end = string.size();
    while (begin < end && isTrimmed(string.at(begin))) {
        ++begin;
    }
    while (end > begin && isTrimmed(string.at(end - 1))) {
        --end;
    }
    return string.mid(begin, end - begin);
}

qsizetype utf8Length(char32_t codePoint)
{
    if (codePoint < 0x80) {
        return 1;
    }
    if (codePoint < 0x800) {
        return 2;
    }
    if (codePoint < 0x10000) {
        return 3;
    }
    return 4;
}

QString composeFileName(const QString &stem, const QString &suffix, const QString &extension)
{
    const QString tail = suffix + QLatin1Char('.') + extension;
    const qsizetype stemBudget = MaxFileNameBytes - tail.toUtf8().size();
    QString truncatedStem = trimSpacesAndDots(truncateUtf8(stem, stemBudget));
    if (truncatedStem.isEmpty()) {
        truncatedStem = QStringLiteral("_");
    }
    return truncatedStem + tail;
}
}

QString primaryAuthor(const KomgaBook &book)
{
    for (const KomgaAuthor &author : book.authors) {
        if (author.role.compare(QLatin1String("writer"), Qt::CaseInsensitive) == 0 && !author.name.trimmed().isEmpty()) {
            return author.name.trimmed();
        }
    }
    for (const KomgaAuthor &author : book.authors) {
        if (!author.name.trimmed().isEmpty()) {
            return author.name.trimmed();
        }
    }
    return book.seriesTitle.trimmed();
}

QString sanitize(const QString &component)
{
    QString result = component;
    for (QChar &c : result) {
        if (isForbidden(c)) {
            c = QLatin1Char('_');
        }
    }
    result = trimSpacesAndDots(result);
    return result.isEmpty() ? QStringLiteral("_") : result;
}

QString extensionFor(const QString &mediaType)
{
    if (mediaType == MediaTypeEpub || isComicMediaType(mediaType)) {
        return QStringLiteral("kepub.epub");
    }
    if (mediaType == MediaTypePdf) {
        return QStringLiteral("pdf");
    }
    return {};
}

QString truncateUtf8(const QString &string, qsizetype maxBytes)
{
    qsizetype bytes = 0;
    qsizetype index = 0;
    while (index < string.size()) {
        qsizetype units = 1;
        char32_t codePoint = string.at(index).unicode();
        if (string.at(index).isHighSurrogate() && index + 1 < string.size() && string.at(index + 1).isLowSurrogate()) {
            codePoint = QChar::surrogateToUcs4(string.at(index), string.at(index + 1));
            units = 2;
        }
        const qsizetype length = utf8Length(codePoint);
        if (bytes + length > maxBytes) {
            break;
        }
        bytes += length;
        index += units;
    }
    return string.left(index);
}

QStringList candidateFileNames(const KomgaBook &book)
{
    const QString extension = extensionFor(book.mediaType);
    if (extension.isEmpty()) {
        return {};
    }

    QString title = book.title.trimmed();
    if (title.isEmpty()) {
        title = book.name.trimmed();
    }
    if (title.isEmpty()) {
        title = book.id;
    }
    // A local file keeps its own name (SPEC-LOCAL.md §3).
    if (LocalFiles::isLocalId(book.id)) {
        const QString stem = sanitize(title);
        QStringList names{composeFileName(stem, QString(), extension)};
        for (int n = 2; n <= 9; ++n) {
            names.append(composeFileName(stem, QStringLiteral(" [%1]").arg(n), extension));
        }
        return names;
    }

    QString author = primaryAuthor(book);
    if (author.isEmpty()) {
        author = QStringLiteral("Unknown");
    }

    const QString stem = sanitize(author) + QLatin1String(" - ") + sanitize(title);
    return {
        composeFileName(stem, QString(), extension),
        composeFileName(stem, QStringLiteral(" [%1]").arg(sanitize(book.id.left(6))), extension),
        composeFileName(stem, QStringLiteral(" [%1]").arg(sanitize(book.id)), extension),
    };
}

} // namespace Kobold::FileNaming
