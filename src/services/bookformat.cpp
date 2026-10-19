// SPDX-License-Identifier: MIT

#include "bookformat.h"

#include "komgabook.h"
#include "syncexecutor.h"

namespace Kobold
{

QString formatName(const QString &mediaType)
{
    if (mediaType == MediaTypeEpub) {
        return QStringLiteral("EPUB");
    }
    if (mediaType == MediaTypePdf) {
        return QStringLiteral("PDF");
    }
    if (mediaType == QLatin1String("application/zip")) {
        return QStringLiteral("CBZ");
    }
    if (mediaType.startsWith(QLatin1String("application/x-rar-compressed")) || mediaType == QLatin1String("application/vnd.rar")) {
        return QStringLiteral("CBR");
    }
    return mediaType.isEmpty() ? QStringLiteral("?") : mediaType;
}

QString formatName(const KomgaBook &book)
{
    if (!book.mediaType.isEmpty()) {
        return formatName(book.mediaType);
    }
    const QString extension = book.url.section(QLatin1Char('.'), -1).toUpper();
    return extension.isEmpty() || extension == book.url.toUpper() ? QStringLiteral("?") : extension + QLatin1Char('?');
}

QString bookLine(const KomgaBook &book, bool selected, const QString &indent)
{
    return QStringLiteral("%1%2 %3  %4  %5")
        .arg(indent, selected ? QStringLiteral("*") : QStringLiteral(" "), book.id.leftJustified(13), formatName(book).leftJustified(5), bookLabel(book));
}

} // namespace Kobold
