// SPDX-License-Identifier: MIT

#include "komgabook.h"

#include <QJsonDocument>
#include <QTest>
#include <QTimeZone>

using namespace Kobold;

class KomgaBookTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesBookDto()
    {
        const QByteArray json = R"({
            "id": "0ABC",
            "name": "the-dispossessed",
            "seriesTitle": "Hainish Cycle",
            "fileHash": "abc123",
            "fileLastModified": "2026-09-01T10:00:00.5Z",
            "sizeBytes": 3000000000,
            "media": {"mediaType": "application/epub+zip", "status": "READY", "pagesCount": 412, "epubIsKepub": true},
            "metadata": {"title": "The Dispossessed", "authors": [{"name": "Ursula K. Le Guin", "role": "writer"}]},
            "readProgress": {"page": 12, "completed": false, "readDate": "2026-10-01T08:00:00Z", "lastModified": "2026-10-01T08:00:01Z"}
        })";
        const auto book = KomgaBook::fromJson(QJsonDocument::fromJson(json).object());
        QVERIFY(book);
        QCOMPARE(book->id, QStringLiteral("0ABC"));
        QCOMPARE(book->seriesTitle, QStringLiteral("Hainish Cycle"));
        QCOMPARE(book->title, QStringLiteral("The Dispossessed"));
        QCOMPARE(book->authors.size(), 1);
        QCOMPARE(book->authors.first().role, QStringLiteral("writer"));
        QCOMPARE(book->mediaType, QStringLiteral("application/epub+zip"));
        QCOMPARE(book->mediaStatus, QStringLiteral("READY"));
        QCOMPARE(book->pagesCount, 412);
        QVERIFY(book->epubIsKepub);
        QCOMPARE(book->sizeBytes, Q_INT64_C(3000000000));
        QCOMPARE(book->fileLastModified, QDateTime(QDate(2026, 9, 1), QTime(10, 0, 0, 500), QTimeZone::UTC));
        QVERIFY(book->readProgress);
        QCOMPARE(book->readProgress->page, 12);
        QVERIFY(!book->readProgress->completed);
        QVERIFY(book->readProgress->readDate.isValid());
    }

    void readProgressIsOptional()
    {
        const auto book = KomgaBook::fromJson(QJsonDocument::fromJson(R"({"id": "X", "readProgress": null})").object());
        QVERIFY(book);
        QVERIFY(!book->readProgress);
    }

    void missingIdIsRejected()
    {
        QVERIFY(!KomgaBook::fromJson(QJsonObject()));
    }

    void supportedMediaTypes()
    {
        QVERIFY(isSupportedMediaType(QStringLiteral("application/epub+zip")));
        QVERIFY(isSupportedMediaType(QStringLiteral("application/pdf")));
        QVERIFY(isSupportedMediaType(QStringLiteral("application/zip")));
        QVERIFY(isComicMediaType(QStringLiteral("application/x-rar-compressed; version=4")));
        QVERIFY(!isComicMediaType(MediaTypeEpub));
        QVERIFY(!isSupportedMediaType(QStringLiteral("application/x-mobipocket-ebook")));
        QVERIFY(!isSupportedMediaType(QString()));
    }
};

QTEST_GUILESS_MAIN(KomgaBookTest)

#include "komgabooktest.moc"
