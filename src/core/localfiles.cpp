// SPDX-License-Identifier: MIT

#include "localfiles.h"

#include <QCryptographicHash>
#include <QFileInfo>

namespace Kobold::LocalFiles
{

bool isLocalId(const QString &id)
{
    return id.startsWith(IdPrefix);
}

QString idFor(const QString &absolutePath)
{
    const QByteArray digest = QCryptographicHash::hash(absolutePath.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QString(IdPrefix) + QString::fromLatin1(digest.left(16));
}

QString mediaTypeFor(const QString &path)
{
    const QString lower = path.toLower();
    if (lower.endsWith(QLatin1String(".epub"))) {
        return MediaTypeEpub;
    }
    if (lower.endsWith(QLatin1String(".pdf"))) {
        return MediaTypePdf;
    }
    if (lower.endsWith(QLatin1String(".cbz"))) {
        return QStringLiteral("application/zip");
    }
    if (lower.endsWith(QLatin1String(".cbr"))) {
        return QStringLiteral("application/x-rar-compressed");
    }
    return {};
}

QString baseName(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    for (const QLatin1StringView extension : {QLatin1StringView(".kepub.epub"),
                                              QLatin1StringView(".epub"),
                                              QLatin1StringView(".pdf"),
                                              QLatin1StringView(".cbz"),
                                              QLatin1StringView(".cbr")}) {
        if (name.endsWith(extension, Qt::CaseInsensitive)) {
            name.chop(extension.size());
            break;
        }
    }
    return name;
}

std::optional<KomgaBook> describe(const QString &absolutePath)
{
    const QFileInfo info(absolutePath);
    if (!info.isFile()) {
        return std::nullopt;
    }
    KomgaBook book;
    book.id = idFor(absolutePath);
    book.name = baseName(absolutePath);
    book.title = book.name;
    book.mediaType = mediaTypeFor(absolutePath);
    book.mediaStatus = MediaStatusReady;
    book.epubIsKepub = absolutePath.endsWith(QLatin1String(".kepub.epub"), Qt::CaseInsensitive);
    book.sizeBytes = info.size();
    book.fileLastModified = info.lastModified().toUTC();
    book.fileHash = QStringLiteral("%1-%2").arg(info.size()).arg(info.lastModified().toMSecsSinceEpoch());
    return book;
}

} // namespace Kobold::LocalFiles
