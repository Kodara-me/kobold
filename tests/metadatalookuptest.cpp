// SPDX-License-Identifier: MIT

#include "fakehttpserver.h"
#include "metadatalookup.h"

#include <QNetworkAccessManager>
#include <QTest>
#include <QUrlQuery>

using namespace Kobold;
using namespace Kobold::Testing;

namespace
{
const QByteArray OpenLibrary = R"({"docs": [{"key": "/works/OL1W", "title": "Le Griffon Noir", "author_name": ["Mercedes Lackey"],
    "first_publish_year": 1994, "publisher": ["Pocket"], "language": ["fre"], "subject": ["Fantasy", "Magic"],
    "isbn": ["2266000000", "9782266000000"], "cover_i": 42}]})";

const QByteArray Google = R"({"items": [{"id": "G1", "volumeInfo": {"title": "Le Griffon Noir", "authors": ["Mercedes Lackey"],
    "publisher": "Pocket", "publishedDate": "2003-05", "description": "<p>A <b>mage</b>.</p>", "categories": ["Fiction"],
    "language": "fr", "maturityRating": "NOT_MATURE",
    "industryIdentifiers": [{"type": "ISBN_10", "identifier": "2266000000"}, {"type": "ISBN_13", "identifier": "9782266000001"}],
    "imageLinks": {"thumbnail": "http://books.google.com/cover.jpg"}}}]})";

const QByteArray ComicVineIssues = R"({"results": [{"id": 77, "name": "", "issue_number": "3", "volume": {"name": "Astérix"},
    "cover_date": "1963-01-01", "description": "<p>Gaul.</p>", "image": {"medium_url": "https://cv/3.jpg"}}]})";

const QByteArray ComicVineVolumes = R"({"results": [{"id": 9, "name": "Astérix", "start_year": "1961", "publisher": {"name": "Dargaud"},
    "deck": "Gauls against Rome.", "image": {"medium_url": "https://cv/a.jpg"}}]})";
}

class MetadataLookupTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesOpenLibrary()
    {
        const QList<LookupResult> results = MetadataLookup::parseOpenLibrary(OpenLibrary, QUrl(QStringLiteral("https://covers.example")), false);
        QCOMPARE(results.size(), 1);
        const BookMetadata &m = results.first().metadata;
        QCOMPARE(m.title, QStringLiteral("Le Griffon Noir"));
        QCOMPARE(m.authors, QStringList({QStringLiteral("Mercedes Lackey")}));
        QCOMPARE(m.language, QStringLiteral("fr"));
        QCOMPARE(m.isbn, QStringLiteral("9782266000000"));
        QCOMPARE(m.releaseDate, QDate(1994, 1, 1));
        QCOMPARE(results.first().coverUrl, QUrl(QStringLiteral("https://covers.example/b/id/42-L.jpg")));
        QVERIFY(results.first().label.contains(QLatin1String("Open Library")));
    }

    void parsesGoogleBooks()
    {
        const QList<LookupResult> results = MetadataLookup::parseGoogleBooks(Google, true);
        QCOMPARE(results.size(), 1);
        const LookupResult &result = results.first();
        QCOMPARE(result.metadata.summary, QStringLiteral("A mage."));
        QCOMPARE(result.metadata.releaseDate, QDate(2003, 5, 1));
        QCOMPARE(result.metadata.isbn, QStringLiteral("9782266000001"));
        QCOMPARE(result.metadata.ageRating, -1);
        QCOMPARE(result.coverUrl.scheme(), QStringLiteral("https"));
        // For a series, the summary and genres are proposed to the series too.
        QCOMPARE(result.series.summary, QStringLiteral("A mage."));
        QCOMPARE(result.series.genres, QStringList({QStringLiteral("Fiction")}));
    }

    void parsesComicVine()
    {
        const QList<LookupResult> issues = MetadataLookup::parseComicVine(ComicVineIssues, false);
        QCOMPARE(issues.size(), 1);
        QCOMPARE(issues.first().metadata.series, QStringLiteral("Astérix"));
        QCOMPARE(*issues.first().metadata.number, 3.0);
        QCOMPARE(issues.first().metadata.title, QStringLiteral("Astérix #3"));
        QCOMPARE(issues.first().metadata.summary, QStringLiteral("Gaul."));

        const QList<LookupResult> volumes = MetadataLookup::parseComicVine(ComicVineVolumes, true);
        QCOMPARE(volumes.first().series.name, QStringLiteral("Astérix"));
        QCOMPARE(volumes.first().series.summary, QStringLiteral("Gauls against Rome."));
        QCOMPARE(volumes.first().metadata.publisher, QStringLiteral("Dargaud"));
    }

    // Books: Open Library, then Google Books; comics: ComicVine with a key, the books sources
    // without one. Nothing else is asked.
    void searchesInOrder()
    {
        FakeHttpServer server;
        server.setResponse("GET /search.json", {200, OpenLibrary});
        server.setResponse("GET /books/v1/volumes", {200, Google});
        server.setResponse("GET /api/search/", {200, ComicVineIssues});
        QNetworkAccessManager network;
        MetadataLookup lookup(&network);
        for (const QString &source : {QStringLiteral("openlibrary"), QStringLiteral("openlibrary-covers"), QStringLiteral("google"), QStringLiteral("comicvine")}) {
            lookup.setBaseUrl(source, server.url());
        }

        QList<LookupResult> results;
        bool done = false;
        lookup.search(LookupQuery{QString(), QStringLiteral("Le Griffon Noir"), QStringLiteral("Lackey"), false, false},
                      [&](const QList<LookupResult> &found, const QString &) {
                          results = found;
                          done = true;
                      });
        QTRY_VERIFY(done);
        QCOMPARE(results.size(), 2);
        QCOMPARE(results.at(0).source, QStringLiteral("openlibrary"));
        QCOMPARE(results.at(1).source, QStringLiteral("google"));
        const QUrlQuery google(QUrl(QString::fromLatin1(server.requests().last().target)));
        QCOMPARE(google.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded), QStringLiteral("intitle:\"Le Griffon Noir\" inauthor:\"Lackey\""));

        // A comic without a ComicVine key goes to the books sources.
        done = false;
        lookup.search(LookupQuery{QString(), QStringLiteral("Astérix"), QString(), true, false}, [&](const QList<LookupResult> &found, const QString &) {
            results = found;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(results.first().source, QStringLiteral("openlibrary"));
        for (const auto &request : server.requests()) {
            QVERIFY(!request.target.startsWith("/api/search/"));
        }

        lookup.setComicVineKey(QStringLiteral("KEY"));
        done = false;
        lookup.search(LookupQuery{QString(), QStringLiteral("Astérix"), QString(), true, false}, [&](const QList<LookupResult> &found, const QString &) {
            results = found;
            done = true;
        });
        QTRY_VERIFY(done);
        QCOMPARE(results.size(), 1);
        QCOMPARE(results.first().source, QStringLiteral("comicvine"));
        QCOMPARE(server.requests().last().headers.value("user-agent"), QByteArray("Kobold"));

        // Neither a title nor an ISBN: nothing is sent.
        const qsizetype sent = server.requests().size();
        QString error;
        lookup.search(LookupQuery{}, [&](const QList<LookupResult> &, const QString &message) {
            error = message;
        });
        QVERIFY(!error.isEmpty());
        QCOMPARE(server.requests().size(), sent);
    }
};

QTEST_MAIN(MetadataLookupTest)

#include "metadatalookuptest.moc"
