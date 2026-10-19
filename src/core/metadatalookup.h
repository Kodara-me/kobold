// SPDX-License-Identifier: MIT

#pragma once

#include "shelfmetadata.h"

#include <QObject>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;

namespace Kobold
{

// What to look for (SPEC-SHELF.md §4.3): by ISBN when known, otherwise by title and author.
struct LookupQuery {
    QString isbn;
    QString title;
    QString author;
    // A comic: ComicVine when it has a key. A series: series results.
    bool comic = false;
    bool series = false;
};

struct LookupResult {
    // "openlibrary", "google" or "comicvine".
    QString source;
    QString id;
    // "Title — Author (year)", for the list.
    QString label;
    // For a book; for a series, summary, genres and age go into series too.
    BookMetadata metadata;
    SeriesMetadata series;
    QUrl coverUrl;
};

// Searches Open Library and Google Books (no account), and ComicVine with the user's key, on
// request only. Nothing is sent otherwise.
class MetadataLookup : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(const QList<LookupResult> &results, const QString &errorString)>;

    explicit MetadataLookup(QNetworkAccessManager *network, QObject *parent = nullptr);

    void setComicVineKey(const QString &key);
    // Tests: the base address of "openlibrary", "openlibrary-covers", "google" or "comicvine".
    void setBaseUrl(const QString &source, const QUrl &url);

    // Books: Open Library, then Google Books. Comics: ComicVine when it has a key, otherwise as
    // books. Calls done once, with every result in that order; errorString when none came.
    void search(const LookupQuery &query, Done done);
    // Downloads a cover found by search(); empty on error.
    void fetchCover(const QUrl &url, std::function<void(const QByteArray &data)> done);

    // Pure parsers of the answers, for the tests.
    static QList<LookupResult> parseOpenLibrary(const QByteArray &json, const QUrl &coversBase, bool series);
    static QList<LookupResult> parseGoogleBooks(const QByteArray &json, bool series);
    static QList<LookupResult> parseComicVine(const QByteArray &json, bool series);

private:
    void get(const QUrl &url, std::function<void(const QByteArray &data, const QString &errorString)> done);
    QUrl base(const QString &source) const;

    QNetworkAccessManager *m_network;
    QString m_comicVineKey;
    QHash<QString, QUrl> m_bases;
};

} // namespace Kobold
