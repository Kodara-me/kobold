// SPDX-License-Identifier: MIT

#include "epubmetadatawriter.h"

#include <KLocalizedString>
#include <KZip>

#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <memory>

namespace Kobold::EpubMetadataWriter
{

namespace
{
QString escaped(const QString &text)
{
    return text.toHtmlEscaped();
}

void collectFiles(const KArchiveDirectory *directory, const QString &prefix, QStringList *paths)
{
    const QStringList entries = directory->entries();
    for (const QString &entry : entries) {
        const KArchiveEntry *item = directory->entry(entry);
        if (item->isDirectory()) {
            collectFiles(static_cast<const KArchiveDirectory *>(item), prefix + entry + QLatin1Char('/'), paths);
        } else {
            paths->append(prefix + entry);
        }
    }
}

QString opfPath(const KZip &zip)
{
    const KArchiveFile *container = zip.directory()->file(QStringLiteral("META-INF/container.xml"));
    if (!container) {
        return {};
    }
    QXmlStreamReader xml(container->data());
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == QLatin1String("rootfile")) {
            return xml.attributes().value(QLatin1String("full-path")).toString();
        }
    }
    return {};
}
}

QByteArray rewriteOpf(const QByteArray &opf, const EpubMetadata &metadata)
{
    QString text = QString::fromUtf8(opf);
    // The prefix used for Dublin Core elements ("dc:" in practice).
    static const QRegularExpression titlePattern(QStringLiteral("<(\\w+:)?title(\\s[^>]*)?>.*?</\\1?title>"),
                                                 QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression creatorPattern(QStringLiteral("\\s*<(\\w+:)?creator(\\s[^>]*)?>.*?</\\1?creator>"),
                                                   QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression seriesPattern(QStringLiteral("\\s*<meta\\s[^>]*name=\"calibre:series(_index)?\"[^>]*/>"));
    static const QRegularExpression metadataEnd(QStringLiteral("</(\\w+:)?metadata>"));

    const QRegularExpressionMatch titleMatch = titlePattern.match(text);
    const QString prefix = titleMatch.hasMatch() ? titleMatch.captured(1) : QStringLiteral("dc:");
    if (!metadata.title.isEmpty()) {
        const QString title = QStringLiteral("<%1title>%2</%1title>").arg(prefix, escaped(metadata.title));
        if (titleMatch.hasMatch()) {
            text.replace(titleMatch.capturedStart(), titleMatch.capturedLength(), title);
        }
    }
    QString added;
    if (!metadata.authors.isEmpty()) {
        text.remove(creatorPattern);
        for (const QString &author : metadata.authors) {
            added += QStringLiteral("\n    <%1creator opf:role=\"aut\">%2</%1creator>").arg(prefix, escaped(author));
        }
    }
    text.remove(seriesPattern);
    if (!metadata.series.isEmpty()) {
        added += QStringLiteral("\n    <meta name=\"calibre:series\" content=\"%1\"/>").arg(escaped(metadata.series));
        if (metadata.number) {
            added += QStringLiteral("\n    <meta name=\"calibre:series_index\" content=\"%1\"/>").arg(QString::number(*metadata.number));
        }
    }
    const QRegularExpressionMatch end = metadataEnd.match(text);
    if (end.hasMatch()) {
        text.insert(end.capturedStart(), added + QLatin1String("\n  "));
    }
    return text.toUtf8();
}

bool write(const QString &epubPath, const EpubMetadata &metadata, QString *errorString)
{
    const QString temporaryPath = epubPath + QStringLiteral(".meta");
    {
        KZip source(epubPath);
        if (!source.open(QIODevice::ReadOnly)) {
            *errorString = i18n("cannot read %1", epubPath);
            return false;
        }
        const QString opf = opfPath(source);
        if (opf.isEmpty()) {
            *errorString = i18n("%1 has no package document", epubPath);
            return false;
        }
        QStringList files;
        collectFiles(source.directory(), QString(), &files);

        KZip target(temporaryPath);
        if (!target.open(QIODevice::WriteOnly)) {
            *errorString = i18n("cannot write %1", temporaryPath);
            return false;
        }
        // The mimetype entry comes first and is stored uncompressed (EPUB OCF).
        target.setCompression(KZip::NoCompression);
        target.writeFile(QStringLiteral("mimetype"), "application/epub+zip");
        target.setCompression(KZip::DeflateCompression);
        for (const QString &path : std::as_const(files)) {
            if (path == QLatin1String("mimetype")) {
                continue;
            }
            const QByteArray data = source.directory()->file(path)->data();
            target.writeFile(path, path == opf ? rewriteOpf(data, metadata) : data);
        }
        if (!target.close()) {
            QFile::remove(temporaryPath);
            *errorString = i18n("cannot write %1", temporaryPath);
            return false;
        }
    }
    QFile::remove(epubPath);
    if (!QFile::rename(temporaryPath, epubPath)) {
        *errorString = i18n("cannot replace %1", epubPath);
        return false;
    }
    return true;
}

} // namespace Kobold::EpubMetadataWriter
