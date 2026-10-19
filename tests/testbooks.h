// SPDX-License-Identifier: MIT

#pragma once

#include "komgabook.h"

#include <QTimeZone>

namespace Kobold::Testing
{

inline KomgaBook makeBook(const QString &id, const QString &author, const QString &title, const QString &mediaType = QStringLiteral("application/epub+zip"))
{
    KomgaBook book;
    book.id = id;
    book.name = title;
    book.seriesTitle = QStringLiteral("Series");
    book.title = title;
    if (!author.isEmpty()) {
        book.authors.append({author, QStringLiteral("writer")});
    }
    book.mediaType = mediaType;
    book.mediaStatus = QStringLiteral("READY");
    book.pagesCount = 100;
    book.fileHash = QStringLiteral("hash-") + id;
    book.fileLastModified = QDateTime(QDate(2026, 9, 1), QTime(10, 0), QTimeZone::UTC);
    book.sizeBytes = 1000;
    return book;
}

} // namespace Kobold::Testing
