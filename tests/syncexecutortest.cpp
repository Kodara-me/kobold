// SPDX-License-Identifier: MIT

#include "kobobooks.h"
#include "syncexecutor.h"
#include "epubmetadatawriter.h"
#include "localfiles.h"
#include "shelfdatabase.h"
#include "shelflibrary.h"
#include "comicconverter.h"
#include "converter.h"
#include "fakehttpserver.h"
#include "komgaclient.h"
#include "manifest.h"

#include <KZip>

#include <QBuffer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;
using namespace std::chrono_literals;

namespace
{
// Converter stub: copies its input to its output, or fails when the input contains "BROKEN".
const QByteArray ConverterStub = R"(#!/bin/sh
if grep -q BROKEN "$3"; then echo "invalid epub" >&2; exit 2; fi
cp "$3" "$2"
)";

// KCC stub: copies the archive (last argument) as the single KEPUB in the output directory
// (8th argument) and records its arguments in $KOBOLD_KCC_ARGS.
const QByteArray KccStub = R"(#!/bin/sh
printf '%s\n' "$@" > "$KOBOLD_KCC_ARGS"
prev=""
for arg in "$@"; do
    [ "$prev" = "--output" ] && out="$arg"
    prev="$arg"
    archive="$arg"
done
cp "$archive" "$out/converted.kepub.epub"
)";

QByteArray bookJson(const QString &id, const QString &title, const QString &mediaType, const QString &hash)
{
    const QJsonObject book{
        {QLatin1String("id"), id},
        {QLatin1String("name"), title},
        {QLatin1String("seriesId"), QString(QStringLiteral("SER-") + id)},
        {QLatin1String("seriesTitle"), QStringLiteral("Series")},
        {QLatin1String("fileHash"), hash},
        {QLatin1String("fileLastModified"), QStringLiteral("2026-09-01T10:00:00Z")},
        {QLatin1String("sizeBytes"), 100},
        {QLatin1String("media"),
         QJsonObject{{QLatin1String("mediaType"), mediaType}, {QLatin1String("status"), QStringLiteral("READY")}, {QLatin1String("pagesCount"), 10}}},
        {QLatin1String("metadata"),
         QJsonObject{{QLatin1String("title"), title},
                     {QLatin1String("authors"),
                      QJsonArray{QJsonObject{{QLatin1String("name"), QStringLiteral("Author")}, {QLatin1String("role"), QStringLiteral("writer")}}}}}},
    };
    return QJsonDocument(book).toJson(QJsonDocument::Compact);
}
}

class SyncExecutorTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<KomgaClient> m_client;
    std::unique_ptr<Converter> m_converter;
    std::unique_ptr<ComicConverter> m_comicConverter;
    QString m_device;
    // Comic settings applied by sync().
    bool m_withComicConverter = true;
    QString m_comicProfile = QStringLiteral("KoC");
    QString m_comicReason;

    void serveBook(const QString &id, const QString &title, const QString &mediaType, const QByteArray &content, const QString &hash = QStringLiteral("h1"))
    {
        m_server->setResponse("GET /api/v1/books/" + id.toUtf8(), {200, bookJson(id, title, mediaType, hash)});
        m_server->setResponse("GET /api/v1/books/" + id.toUtf8() + "/file", {200, content, "application/octet-stream"});
        setSeriesDirection(id, QString());
    }

    void setSeriesDirection(const QString &bookId, const QString &direction)
    {
        m_server->setResponse("GET /api/v1/series/SER-" + bookId.toUtf8(),
                              {200,
                               QJsonDocument(QJsonObject{{QLatin1String("id"), QString(QStringLiteral("SER-") + bookId)},
                                                         {QLatin1String("metadata"), QJsonObject{{QLatin1String("readingDirection"), direction}}}})
                                   .toJson(QJsonDocument::Compact)});
    }

    // The Kobo's serial: empty, the Kobo's library is never changed (most tests).
    QString m_serial;

    SyncReport sync(const QStringList &selection, bool dryRun = false, const QString &komgaUrl = QStringLiteral("https://comics.example"))
    {
        SyncExecutor executor(m_client.get(), m_converter.get());
        if (m_withComicConverter) {
            executor.setComicConverter(m_comicConverter.get());
        }
        std::optional<SyncReport> report;
        connect(&executor, &SyncExecutor::finished, this, [&report](const SyncReport &r) {
            report = r;
        });
        SyncExecutor::Options options;
        options.deviceRoot = m_device;
        options.booksFolder = QStringLiteral("kobold");
        options.komgaUrl = komgaUrl;
        options.selection = selection;
        options.dryRun = dryRun;
        options.comicProfile = m_comicProfile;
        options.comicUnavailableReason = m_comicReason;
        options.serial = m_serial;
        options.backupsRoot = m_dir->filePath(QStringLiteral("backups"));
        executor.run(options);
        [&]() {
            QTRY_VERIFY_WITH_TIMEOUT(report, 20000);
        }();
        if (!report) {
            SyncReport missing;
            missing.outcome = SyncReport::Outcome::Error;
            missing.errorString = QStringLiteral("no report");
            return missing;
        }
        return *report;
    }

    // A sync with Removals::Ask; answer gets the questions and answers them (SPEC-UNSYNC.md).
    SyncReport syncAsking(const QStringList &selection, const std::function<void(SyncExecutor *, const QList<RemovalQuestion> &)> &answer)
    {
        SyncExecutor executor(m_client.get(), m_converter.get());
        std::optional<SyncReport> report;
        connect(&executor, &SyncExecutor::finished, this, [&report](const SyncReport &r) {
            report = r;
        });
        connect(&executor, &SyncExecutor::removalsToConfirm, this, [&executor, &answer](const QList<RemovalQuestion> &questions) {
            answer(&executor, questions);
        });
        SyncExecutor::Options options;
        options.deviceRoot = m_device;
        options.booksFolder = QStringLiteral("kobold");
        options.komgaUrl = QStringLiteral("https://comics.example");
        options.selection = selection;
        options.removals = SyncExecutor::Options::Removals::Ask;
        executor.run(options);
        [&]() {
            QTRY_VERIFY_WITH_TIMEOUT(report, 20000);
        }();
        return report.value_or(SyncReport{});
    }

    // A sync with local files; with answer, removals are asked (SPEC-LOCAL.md).
    SyncReport syncLocal(const QStringList &localFiles,
                         const std::function<void(SyncExecutor *, const QList<RemovalQuestion> &)> &answer = {},
                         const QStringList &selection = {})
    {
        SyncExecutor executor(m_client.get(), m_converter.get());
        std::optional<SyncReport> report;
        connect(&executor, &SyncExecutor::finished, this, [&report](const SyncReport &r) {
            report = r;
        });
        connect(&executor, &SyncExecutor::removalsToConfirm, this, [&executor, &answer](const QList<RemovalQuestion> &questions) {
            answer(&executor, questions);
        });
        SyncExecutor::Options options;
        options.deviceRoot = m_device;
        options.booksFolder = QStringLiteral("kobold");
        options.komgaUrl = QStringLiteral("https://comics.example");
        options.selection = selection;
        options.localFiles = localFiles;
        options.removals = answer ? SyncExecutor::Options::Removals::Ask : SyncExecutor::Options::Removals::Automatic;
        executor.run(options);
        [&]() {
            QTRY_VERIFY_WITH_TIMEOUT(report, 20000);
        }();
        return report.value_or(SyncReport{});
    }

    QString localFile(const QString &name, const QByteArray &content)
    {
        const QString path = m_dir->filePath(QStringLiteral("computer/") + name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(content);
        }
        return path;
    }

    QString devicePath(const QString &relative) const
    {
        return QDir(m_device).filePath(relative);
    }

    QByteArray readDeviceFile(const QString &relative) const
    {
        QFile file(devicePath(relative));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    int fileDownloads() const
    {
        return std::count_if(m_server->requests().cbegin(), m_server->requests().cend(), [](const FakeHttpServer::Request &request) {
            return request.target.endsWith("/file");
        });
    }

    QStringList leftovers() const
    {
        QStringList found;
        QDirIterator it(m_device, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.endsWith(QLatin1String(".part"))) {
                found.append(path);
            }
        }
        return found;
    }

    Manifest loadManifest() const
    {
        Manifest manifest(m_device);
        manifest.load();
        return manifest;
    }

private Q_SLOTS:
    void init()
    {
        m_dir = std::make_unique<QTemporaryDir>();
        m_device = m_dir->filePath(QStringLiteral("KOBOeReader"));
        QVERIFY(QDir().mkpath(m_device + QStringLiteral("/.kobo")));
        QFile database(m_device + QStringLiteral("/.kobo/KoboReader.sqlite"));
        QVERIFY(database.open(QIODevice::WriteOnly));

        const QString stub = m_dir->filePath(QStringLiteral("kepubify-stub"));
        QFile stubFile(stub);
        QVERIFY(stubFile.open(QIODevice::WriteOnly));
        stubFile.write(ConverterStub);
        stubFile.close();
        QVERIFY(stubFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        m_server = std::make_unique<FakeHttpServer>();
        m_network = std::make_unique<QNetworkAccessManager>();
        m_client = std::make_unique<KomgaClient>(m_network.get());
        m_client->setBaseUrl(m_server->url());
        m_client->setApiKey(QStringLiteral("key"));
        m_converter = std::make_unique<Converter>();
        m_converter->setExecutable(stub);
        m_converter->setTimeout(10s);

        const QString kccStub = m_dir->filePath(QStringLiteral("kcc-stub"));
        QFile kccFile(kccStub);
        QVERIFY(kccFile.open(QIODevice::WriteOnly));
        kccFile.write(KccStub);
        kccFile.close();
        QVERIFY(kccFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        qputenv("KOBOLD_KCC_ARGS", m_dir->filePath(QStringLiteral("kcc-args.txt")).toUtf8());
        m_comicConverter = std::make_unique<ComicConverter>();
        m_comicConverter->setExecutable(kccStub);
        m_comicConverter->setTimeout(10s);
        m_withComicConverter = true;
        m_comicProfile = QStringLiteral("KoC");
        m_comicReason.clear();
        serveBook(QStringLiteral("MOBI1"), QStringLiteral("Mobi"), QStringLiteral("application/x-mobipocket-ebook"), "mobi");
        serveBook(QStringLiteral("CBR1"), QStringLiteral("Rar comic"), QStringLiteral("application/x-rar-compressed; version=4"), "rar comic");

        serveBook(QStringLiteral("EPUB1"), QStringLiteral("First"), QStringLiteral("application/epub+zip"), "epub one");
        serveBook(QStringLiteral("EPUB2"), QStringLiteral("Second"), QStringLiteral("application/epub+zip"), "epub two");
        serveBook(QStringLiteral("PDF1"), QStringLiteral("Paper"), QStringLiteral("application/pdf"), "%PDF-1.7 bytes");
        serveBook(QStringLiteral("CBZ1"), QStringLiteral("Comic"), QStringLiteral("application/zip"), "zip");
    }

    void cleanup()
    {
        m_converter.reset();
        m_comicConverter.reset();
        m_client.reset();
        m_network.reset();
        m_server.reset();
        m_dir.reset();
    }

    // AC 3, 5, 6 (MOBI stays unsupported)
    void firstSyncAddsSupportedBooks()
    {
        const SyncReport report = sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1"), QStringLiteral("MOBI1")});
        QCOMPARE(report.outcome, SyncReport::Outcome::Completed);
        QVERIFY2(report.failures.isEmpty(), qPrintable(report.failures.join(QLatin1Char('\n'))));
        QCOMPARE(report.added, 2);
        QCOMPARE(report.skipped, 1);

        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - First.kepub.epub")), QByteArray("epub one"));
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - Paper.pdf")), QByteArray("%PDF-1.7 bytes"));
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("kobold/Author - Mobi.mobi"))));

        const Manifest manifest = loadManifest();
        QCOMPARE(manifest.entries().size(), 2);
        QCOMPARE(manifest.komgaUrl(), QStringLiteral("https://comics.example"));
        QCOMPARE(manifest.findByBookId(QStringLiteral("PDF1"))->outputSizeBytes, 14);
        QVERIFY(leftovers().isEmpty());
    }

    // AC 4
    void secondSyncTransfersNothing()
    {
        sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1")});
        const int downloads = fileDownloads();
        const SyncReport report = sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1")});
        QCOMPARE(report.added, 0);
        QCOMPARE(report.updated, 0);
        QCOMPARE(report.failed, 0);
        QCOMPARE(fileDownloads(), downloads);
    }

    // An empty manifest (the Kobo unplugged while it was written): the next sync takes the
    // books back over their own files, without a second copy or an error.
    void emptyManifestTakesBooksBack()
    {
        QCOMPARE(sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1")}).added, 2);
        QFile manifest(devicePath(Manifest::relativeFilePath()));
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        manifest.close();

        const SyncReport report = sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1")});
        QVERIFY2(report.failures.isEmpty(), qPrintable(report.failures.join(QLatin1Char('\n'))));
        QCOMPARE(report.added, 0);
        QCOMPARE(report.updated, 2);
        QCOMPARE(loadManifest().entries().size(), 2);
        QCOMPARE(QDir(devicePath(QStringLiteral("kobold"))).entryList(QDir::Files).size(), 2);

        // Back to normal: nothing more to do.
        const SyncReport again = sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1")});
        QCOMPARE(again.added + again.updated, 0);
    }

    // SPEC-KOBO-BOOKS.md §3.2: once the Kobo imported a book, the next sync sets its series in
    // the Kobo's library (SeriesID included), after a backup; nothing more the time after.
    void setsTheSeriesTheKoboGroupsBy()
    {
        const SyncReport first = sync({QStringLiteral("EPUB1")});
        QCOMPARE(first.added, 1);
        // Copied with a series: the user is shown the reconnection steps.
        QCOMPARE(first.seriesPending, 1);
        const QString path = loadManifest().findByBookId(QStringLiteral("EPUB1"))->path;
        const QString database = devicePath(QStringLiteral(".kobo/KoboReader.sqlite"));
        QDir().mkpath(devicePath(QStringLiteral(".kobo")));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("kobo-series"));
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE content (ContentID TEXT, ContentType TEXT, Title TEXT, Series TEXT, SeriesNumber TEXT, "
                                              "SeriesID TEXT, SeriesNumberFloat REAL)")));
            query.prepare(QStringLiteral("INSERT INTO content VALUES (?, '6', 'First', '', '', '', 0)"));
            query.addBindValue(QString(QStringLiteral("file:///mnt/onboard/") + path));
            QVERIFY(query.exec());
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("kobo-series"));

        m_serial = QStringLiteral("N1");
        const SyncReport report = sync({QStringLiteral("EPUB1")});
        QCOMPARE(report.seriesSet, 1);
        // Imported and grouped: no reconnection left to do.
        QCOMPARE(report.seriesPending, 0);
        // The manifest keeps the series, for the next connection without Komga.
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("EPUB1"))->series, QStringLiteral("Series"));
        QCOMPARE(KoboBooks::manifestSeries(m_device).value(path).name, QStringLiteral("Series"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("kobo-series"));
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("SELECT Series, SeriesID FROM content")) && query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("Series"));
            QCOMPARE(query.value(1).toString(), QStringLiteral("Series"));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("kobo-series"));
        QCOMPARE(QDir(m_dir->filePath(QStringLiteral("backups/N1"))).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 1);

        QCOMPARE(sync({QStringLiteral("EPUB1")}).seriesSet, 0);
        m_serial.clear();
    }

    // AC 7
    void changedHashReplacesFile()
    {
        sync({QStringLiteral("EPUB1")});
        serveBook(QStringLiteral("EPUB1"), QStringLiteral("First"), QStringLiteral("application/epub+zip"), "epub one v2", QStringLiteral("h2"));
        const SyncReport report = sync({QStringLiteral("EPUB1")});
        QCOMPARE(report.updated, 1);
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - First.kepub.epub")), QByteArray("epub one v2"));
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("EPUB1"))->komgaFileHash, QStringLiteral("h2"));
    }

    // AC 8
    void deletedFileIsRestored()
    {
        sync({QStringLiteral("EPUB1")});
        QVERIFY(QFile::remove(devicePath(QStringLiteral("kobold/Author - First.kepub.epub"))));
        const SyncReport report = sync({QStringLiteral("EPUB1")});
        QCOMPARE(report.updated, 1);
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - First.kepub.epub")), QByteArray("epub one"));
    }

    // AC 9
    void deselectedBookIsRemoved()
    {
        sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1")});
        const SyncReport report = sync({QStringLiteral("PDF1")});
        QCOMPARE(report.removed, 1);
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("kobold/Author - First.kepub.epub"))));
        QVERIFY(QFile::exists(devicePath(QStringLiteral("kobold/Author - Paper.pdf"))));
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("EPUB1")), nullptr);
    }

    // UAC 1, 2: each removal is asked; Keep leaves the file, marks it, and is not asked again.
    void removalsAreAsked()
    {
        sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1")});
        int asked = 0;
        SyncReport report = syncAsking({}, [&asked](SyncExecutor *executor, const QList<RemovalQuestion> &questions) {
            asked = int(questions.size());
            for (const RemovalQuestion &question : questions) {
                QCOMPARE(question.kind, RemovalQuestion::Case::Deselected);
                QVERIFY(question.removeByDefault());
            }
            executor->answerRemovals({{QStringLiteral("EPUB1"), false}, {QStringLiteral("PDF1"), true}});
        });
        QCOMPARE(asked, 2);
        QCOMPARE(report.removed, 1);
        QCOMPARE(report.kept, 1);
        QVERIFY(QFile::exists(devicePath(QStringLiteral("kobold/Author - First.kepub.epub"))));
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("kobold/Author - Paper.pdf"))));
        QVERIFY(loadManifest().findByBookId(QStringLiteral("EPUB1"))->kept);

        asked = 0;
        report = syncAsking({}, [&asked](SyncExecutor *executor, const QList<RemovalQuestion> &) {
            ++asked;
            executor->answerRemovals({});
        });
        QCOMPARE(asked, 0);
        QVERIFY(QFile::exists(devicePath(QStringLiteral("kobold/Author - First.kepub.epub"))));

        // Selected again: the mark goes away without a download.
        const int downloads = fileDownloads();
        report = sync({QStringLiteral("EPUB1")});
        QVERIFY(!loadManifest().findByBookId(QStringLiteral("EPUB1"))->kept);
        QCOMPARE(fileDownloads(), downloads);
    }

    // UAC 5.
    void cancelChangesNothing()
    {
        sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1")});
        const Manifest before = loadManifest();
        const SyncReport report = syncAsking({QStringLiteral("PDF1"), QStringLiteral("PDF2")}, [](SyncExecutor *executor, const QList<RemovalQuestion> &) {
            executor->cancelRemovals();
        });
        QCOMPARE(report.outcome, SyncReport::Outcome::Cancelled);
        QVERIFY(QFile::exists(devicePath(QStringLiteral("kobold/Author - First.kepub.epub"))));
        QCOMPARE(loadManifest().entries(), before.entries());
    }

    // UAC 3: a book gone from Komga is kept by default.
    void goneFromKomgaIsKeptByDefault()
    {
        sync({QStringLiteral("EPUB1")});
        m_server->setResponse("GET /api/v1/books/EPUB1", {404, "{}"});
        const SyncReport report = syncAsking({QStringLiteral("EPUB1")}, [](SyncExecutor *executor, const QList<RemovalQuestion> &questions) {
            QCOMPARE(questions.size(), 1);
            QCOMPARE(questions.first().kind, RemovalQuestion::Case::GoneFromKomga);
            QVERIFY(!questions.first().removeByDefault());
            executor->answerRemovals({});
        });
        QCOMPARE(report.kept, 1);
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("EPUB1"))->keptReason, QStringLiteral("not in Komga"));
        QVERIFY(QFile::exists(devicePath(QStringLiteral("kobold/Author - First.kepub.epub"))));
    }

    // Automatic answers given in advance: without them, nothing is removed and nothing marked.
    void automaticAnswersLeaveBooksForNow()
    {
        sync({QStringLiteral("EPUB1")});
        SyncExecutor executor(m_client.get(), m_converter.get());
        std::optional<SyncReport> report;
        connect(&executor, &SyncExecutor::finished, this, [&report](const SyncReport &r) {
            report = r;
        });
        SyncExecutor::Options options;
        options.deviceRoot = m_device;
        options.booksFolder = QStringLiteral("kobold");
        options.komgaUrl = QStringLiteral("https://comics.example");
        options.removeDeselected = false;
        executor.run(options);
        QTRY_VERIFY_WITH_TIMEOUT(report, 20000);
        QCOMPARE(report->removed, 0);
        QCOMPARE(report->leftForNow.size(), 1);
        QVERIFY(!loadManifest().findByBookId(QStringLiteral("EPUB1"))->kept);
    }

    // LFAC 1–3, 7: local files go to local/, converted; nothing is copied twice; a change is
    // copied again; the computer files are never changed.
    void localFilesAreSynced()
    {
        const QString epub = localFile(QStringLiteral("A Gift.epub"), "local epub");
        const QString pdf = localFile(QStringLiteral("Notes.pdf"), "%PDF local");
        SyncReport report = syncLocal({epub, pdf});
        QVERIFY2(report.failures.isEmpty(), qPrintable(report.failures.join(QLatin1Char('\n'))));
        QCOMPARE(report.added, 2);
        QCOMPARE(readDeviceFile(QStringLiteral("local/A Gift.kepub.epub")), QByteArray("local epub"));
        QCOMPARE(readDeviceFile(QStringLiteral("local/Notes.pdf")), QByteArray("%PDF local"));
        QCOMPARE(loadManifest().findByBookId(LocalFiles::idFor(pdf))->localPath, pdf);

        report = syncLocal({epub, pdf});
        QCOMPARE(report.added + report.updated, 0);

        QTest::qWait(1100); // A newer modification time.
        localFile(QStringLiteral("Notes.pdf"), "%PDF local v2");
        report = syncLocal({epub, pdf});
        QCOMPARE(report.updated, 1);
        QCOMPARE(readDeviceFile(QStringLiteral("local/Notes.pdf")), QByteArray("%PDF local v2"));
        QFile source(pdf);
        QVERIFY(source.open(QIODevice::ReadOnly));
        QCOMPARE(source.readAll(), QByteArray("%PDF local v2"));
    }

    // LFAC 5: a local file taken out of the selection is case 2, removed by default.
    void deselectedLocalFileIsCase2()
    {
        const QString pdf = localFile(QStringLiteral("Notes.pdf"), "%PDF local");
        syncLocal({pdf});
        const SyncReport report = syncLocal({}, [](SyncExecutor *executor, const QList<RemovalQuestion> &questions) {
            QCOMPARE(questions.size(), 1);
            QCOMPARE(questions.first().kind, RemovalQuestion::Case::LocalDeselected);
            QVERIFY(questions.first().removeByDefault());
            executor->answerRemovals({});
        });
        QCOMPARE(report.removed, 1);
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("local/Notes.pdf"))));
        QVERIFY(QFile::exists(pdf));
    }

    // LFAC 6: a selected local file gone from the computer is case 4, kept by default; Remove
    // takes it out of the selection too.
    void missingLocalFileIsCase4()
    {
        const QString pdf = localFile(QStringLiteral("Notes.pdf"), "%PDF local");
        syncLocal({pdf});
        QVERIFY(QFile::remove(pdf));
        SyncReport report = syncLocal({pdf}, [](SyncExecutor *executor, const QList<RemovalQuestion> &questions) {
            QCOMPARE(questions.size(), 1);
            QCOMPARE(questions.first().kind, RemovalQuestion::Case::LocalMissing);
            QVERIFY(!questions.first().removeByDefault());
            executor->answerRemovals({});
        });
        QCOMPARE(report.kept, 1);
        QVERIFY(QFile::exists(devicePath(QStringLiteral("local/Notes.pdf"))));

        // Kept: not asked again. Remove Now goes through removeManagedBook.
        QString error;
        QVERIFY(removeManagedBook(m_device, LocalFiles::idFor(pdf), &error));
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("local/Notes.pdf"))));

        const QString other = localFile(QStringLiteral("Other.pdf"), "%PDF other");
        syncLocal({other});
        QVERIFY(QFile::remove(other));
        report = syncLocal({other}, [](SyncExecutor *executor, const QList<RemovalQuestion> &questions) {
            executor->answerRemovals({{questions.first().bookId, true}});
        });
        QCOMPARE(report.removed, 1);
        QCOMPARE(report.localFilesToUnselect, QStringList{other});
    }

    void opfIsRewritten()
    {
        const QByteArray opf = "<package><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
                               "<dc:title>Old</dc:title><dc:creator>Someone</dc:creator>"
                               "<meta name=\"calibre:series\" content=\"Old Series\"/><dc:language>fr</dc:language>"
                               "</metadata></package>";
        const QString text = QString::fromUtf8(EpubMetadataWriter::rewriteOpf(opf, EpubMetadata{QStringLiteral("New & Better"), {QStringLiteral("A. Writer")}, QStringLiteral("Saga"), 3}));
        QVERIFY(text.contains(QLatin1String("<dc:title>New &amp; Better</dc:title>")));
        QVERIFY(!text.contains(QLatin1String("Someone")));
        QVERIFY(text.contains(QLatin1String("<dc:creator opf:role=\"aut\">A. Writer</dc:creator>")));
        QVERIFY(text.contains(QLatin1String("content=\"Saga\"")));
        QVERIFY(text.contains(QLatin1String("calibre:series_index\" content=\"3\"")));
        QVERIFY(!text.contains(QLatin1String("Old Series")));
        QVERIFY(text.contains(QLatin1String("<dc:language>fr</dc:language>")));
    }

    // SHAC 7: a shelf book reaches the Kobo with the metadata shown in Kobold.
    void shelfBookCarriesItsMetadata()
    {
        const QString folder = m_dir->filePath(QStringLiteral("shelf"));
        QDir().mkpath(folder);
        {
            KZip zip(folder + QStringLiteral("/griffon.epub"));
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.writeFile(QStringLiteral("mimetype"), "application/epub+zip");
            zip.writeFile(QStringLiteral("META-INF/container.xml"), "<container><rootfiles><rootfile full-path=\"content.opf\"/></rootfiles></container>");
            zip.writeFile(QStringLiteral("content.opf"),
                          "<package><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>griffon</dc:title></metadata></package>");
            zip.close();
        }
        ShelfDatabase database(m_dir->filePath(QStringLiteral("shelves.sqlite")));
        QString error;
        QVERIFY(database.open(&error));
        const auto shelf = database.addShelf(QStringLiteral("Home"), folder, &error);
        QVERIFY(shelf);
        ShelfBook book;
        book.shelfId = shelf->id;
        book.path = QStringLiteral("griffon.epub");
        book.size = QFileInfo(folder + QStringLiteral("/griffon.epub")).size();
        book.modified = QDateTime::currentDateTimeUtc();
        book.fingerprint = QStringLiteral("f1");
        book.mediaType = QStringLiteral("application/epub+zip");
        book.metadata.title = QStringLiteral("Le Griffon Noir");
        book.metadata.authors = {QStringLiteral("Jane Author")};
        book.metadata.series = QStringLiteral("Les Griffons");
        book.metadata.number = 2;
        QVERIFY(database.saveBook(book));

        ShelfLibrary library(&database);
        m_client->setBookSource(&library);
        const QString id = ShelfLibrary::bookId(shelf->id, book.id);
        const SyncReport report = sync({id});
        m_client->setBookSource(nullptr);
        QVERIFY2(report.failures.isEmpty(), qPrintable(report.failures.join(QLatin1Char('\n'))));
        QCOMPARE(report.added, 1);

        const QString copy = devicePath(QStringLiteral("kobold/Jane Author - Le Griffon Noir.kepub.epub"));
        QVERIFY(QFile::exists(copy));
        KZip zip(copy);
        QVERIFY(zip.open(QIODevice::ReadOnly));
        const QString opf = QString::fromUtf8(static_cast<const KArchiveFile *>(zip.directory()->entry(QStringLiteral("content.opf")))->data());
        QVERIFY2(opf.contains(QLatin1String("<dc:title>Le Griffon Noir</dc:title>")), qPrintable(opf));
        QVERIFY(opf.contains(QLatin1String("calibre:series\" content=\"Les Griffons\"")));
        // The original file is untouched.
        KZip original(folder + QStringLiteral("/griffon.epub"));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(static_cast<const KArchiveFile *>(original.directory()->entry(QStringLiteral("content.opf")))->data()).contains(QLatin1String("griffon")));
    }

    // AC 10
    void unmanagedFilesAreNeverTouched()
    {
        QVERIFY(QDir().mkpath(devicePath(QStringLiteral("kobold"))));
        for (const QString &name : {QStringLiteral("kobold/Author - First.kepub.epub"), QStringLiteral("kobold/Mine.epub")}) {
            QFile file(devicePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("mine");
        }

        const SyncReport report = sync({QStringLiteral("EPUB1")});
        QCOMPARE(report.added, 1);
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - First.kepub.epub")), QByteArray("mine"));
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - First [EPUB1].kepub.epub")), QByteArray("epub one"));

        sync({});
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - First.kepub.epub")), QByteArray("mine"));
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Mine.epub")), QByteArray("mine"));
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("kobold/Author - First [EPUB1].kepub.epub"))));
    }

    // AC 11
    void dryRunChangesNothing()
    {
        const SyncReport report = sync({QStringLiteral("EPUB1"), QStringLiteral("PDF1")}, true);
        QCOMPARE(report.plan.size(), 2);
        QCOMPARE(report.added, 0);
        QCOMPARE(fileDownloads(), 0);
        QVERIFY(!QFile::exists(devicePath(Manifest::relativeFilePath())));
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("kobold"))));
    }

    // AC 12
    void conversionFailureDoesNotStopOthers()
    {
        serveBook(QStringLiteral("EPUB2"), QStringLiteral("Second"), QStringLiteral("application/epub+zip"), "BROKEN");
        const SyncReport report = sync({QStringLiteral("EPUB1"), QStringLiteral("EPUB2"), QStringLiteral("PDF1")});
        QCOMPARE(report.added, 2);
        QCOMPARE(report.failed, 1);
        QVERIFY2(report.failures.first().contains(QStringLiteral("Series — Second")), qPrintable(report.failures.first()));
        QVERIFY2(report.failures.first().contains(QLatin1String("invalid epub")), qPrintable(report.failures.first()));

        const Manifest manifest = loadManifest();
        QCOMPARE(manifest.entries().size(), 2);
        QCOMPARE(manifest.findByBookId(QStringLiteral("EPUB2")), nullptr);
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("kobold/Author - Second.kepub.epub"))));
        QVERIFY(leftovers().isEmpty());
    }

    // SPEC-COMICS CAC 2, 3
    void comicsAreConvertedWithTheDeviceProfile()
    {
        const SyncReport report = sync({QStringLiteral("CBZ1"), QStringLiteral("CBR1")});
        QVERIFY2(report.failures.isEmpty(), qPrintable(report.failures.join(QLatin1Char('\n'))));
        QCOMPARE(report.added, 2);
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - Comic.kepub.epub")), QByteArray("zip"));
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - Rar comic.kepub.epub")), QByteArray("rar comic"));

        QFile args(m_dir->filePath(QStringLiteral("kcc-args.txt")));
        QVERIFY(args.open(QIODevice::ReadOnly));
        const QStringList arguments = QString::fromUtf8(args.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(arguments.mid(0, 2), QStringList({QStringLiteral("--profile"), QStringLiteral("KoC")}));
        QVERIFY2(arguments.last().endsWith(QLatin1String(".cbr")), qPrintable(arguments.last()));

        const Manifest manifest = loadManifest();
        QCOMPARE(manifest.findByBookId(QStringLiteral("CBZ1"))->conversionProfile, QStringLiteral("KoC"));

        const int downloads = fileDownloads();
        const SyncReport second = sync({QStringLiteral("CBZ1"), QStringLiteral("CBR1")});
        QCOMPARE(second.added + second.updated, 0);
        QCOMPARE(fileDownloads(), downloads);
    }

    // R1c: a Komga EPUB, converted by kepubify, carries its Komga series like a comic does.
    void convertedEpubCarriesItsSeries()
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        {
            KZip zip(&buffer);
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.writeFile(QStringLiteral("mimetype"), "application/epub+zip");
            zip.writeFile(QStringLiteral("META-INF/container.xml"),
                          "<container><rootfiles><rootfile full-path=\"OEBPS/content.opf\"/></rootfiles></container>");
            zip.writeFile(QStringLiteral("OEBPS/content.opf"),
                          "<package version=\"3.0\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title id=\"id\">Novel</dc:title>"
                          "<dc:creator id=\"id-2\">Writer</dc:creator></metadata></package>");
            zip.close();
        }
        serveBook(QStringLiteral("EPUB9"), QStringLiteral("Novel"), QStringLiteral("application/epub+zip"), buffer.data());
        const SyncReport report = sync({QStringLiteral("EPUB9")});
        QVERIFY2(report.failures.isEmpty(), qPrintable(report.failures.join(QLatin1Char('\n'))));
        QCOMPARE(report.added, 1);

        const QString path = devicePath(loadManifest().findByBookId(QStringLiteral("EPUB9"))->path);
        KZip copy(path);
        QVERIFY(copy.open(QIODevice::ReadOnly));
        const QString opf = QString::fromUtf8(static_cast<const KArchiveFile *>(copy.directory()->entry(QStringLiteral("OEBPS/content.opf")))->data());
        QVERIFY2(opf.contains(QLatin1String("calibre:series\" content=\"Series\"")), qPrintable(opf));
        copy.close();

        // Taken back after a lost manifest: copied again, still with its series.
        QFile manifest(devicePath(Manifest::relativeFilePath()));
        QVERIFY(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
        manifest.close();
        QCOMPARE(sync({QStringLiteral("EPUB9")}).updated, 1);
        KZip again(path);
        QVERIFY(again.open(QIODevice::ReadOnly));
        const QString adopted = QString::fromUtf8(static_cast<const KArchiveFile *>(again.directory()->entry(QStringLiteral("OEBPS/content.opf")))->data());
        QVERIFY2(adopted.contains(QLatin1String("calibre:series\" content=\"Series\"")), qPrintable(adopted));
    }

    // KCC writes no series: the copy gets the Komga series, so that the Kobo groups the volumes.
    void convertedComicCarriesItsSeries()
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        {
            KZip zip(&buffer);
            QVERIFY(zip.open(QIODevice::WriteOnly));
            zip.writeFile(QStringLiteral("mimetype"), "application/epub+zip");
            zip.writeFile(QStringLiteral("META-INF/container.xml"),
                          "<container><rootfiles><rootfile full-path=\"OEBPS/content.opf\"/></rootfiles></container>");
            zip.writeFile(QStringLiteral("OEBPS/content.opf"),
                          "<package><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>Comic</dc:title>"
                          "<dc:creator>KCC</dc:creator></metadata></package>");
            zip.close();
        }
        serveBook(QStringLiteral("CBZ9"), QStringLiteral("Comic"), QStringLiteral("application/zip"), buffer.data());
        const SyncReport report = sync({QStringLiteral("CBZ9")});
        QVERIFY2(report.failures.isEmpty(), qPrintable(report.failures.join(QLatin1Char('\n'))));
        QCOMPARE(report.added, 1);

        KZip copy(devicePath(QStringLiteral("kobold/Author - Comic.kepub.epub")));
        QVERIFY(copy.open(QIODevice::ReadOnly));
        const QString opf = QString::fromUtf8(static_cast<const KArchiveFile *>(copy.directory()->entry(QStringLiteral("OEBPS/content.opf")))->data());
        QVERIFY2(opf.contains(QLatin1String("calibre:series\" content=\"Series\"")), qPrintable(opf));
        // Title and authors stay those of the file.
        QVERIFY(opf.contains(QLatin1String("<dc:creator>KCC</dc:creator>")));
    }

    // SPEC-COMICS CAC 4
    void unknownModelSkipsComicsOnly()
    {
        m_comicProfile.clear();
        m_comicReason = QStringLiteral("unknown Kobo model 999; run: kobold config set-profile <profile>");
        const SyncReport report = sync({QStringLiteral("CBZ1"), QStringLiteral("EPUB1")});
        QCOMPARE(report.added, 1);
        QCOMPARE(report.skipped, 1);
        QCOMPARE(report.failed, 0);
        const auto skip = std::find_if(report.plan.cbegin(), report.plan.cend(), [](const SyncAction &action) {
            return action.type == SyncAction::Type::Skip;
        });
        QCOMPARE(skip->reason, m_comicReason);
    }

    // SPEC-COMICS CAC 6
    void profileChangeReconverts()
    {
        sync({QStringLiteral("CBZ1")});
        m_comicProfile = QStringLiteral("KoL");
        const SyncReport report = sync({QStringLiteral("CBZ1")});
        QCOMPARE(report.updated, 1);
        QCOMPARE(report.plan.first().reason, QStringLiteral("profile changed"));
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("CBZ1"))->conversionProfile, QStringLiteral("KoL"));
    }

    // SPEC-COMICS CAC 7
    void eachDeviceKeepsItsOwnConversions()
    {
        sync({QStringLiteral("CBZ1")});
        const QString first = m_device;

        m_device = m_dir->filePath(QStringLiteral("KOBOeReader1"));
        QVERIFY(QDir().mkpath(m_device + QStringLiteral("/.kobo")));
        m_comicProfile = QStringLiteral("KoE");
        const SyncReport report = sync({QStringLiteral("CBZ1")});
        QCOMPARE(report.added, 1);
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("CBZ1"))->conversionProfile, QStringLiteral("KoE"));

        Manifest firstManifest(first);
        QVERIFY(firstManifest.load());
        QCOMPARE(firstManifest.findByBookId(QStringLiteral("CBZ1"))->conversionProfile, QStringLiteral("KoC"));
    }

    // SPEC-COMICS CAC 8
    void missingKccSkipsComics()
    {
        m_withComicConverter = false;
        m_comicReason = QStringLiteral("kcc not installed; install it with: paru -S kcc");
        const SyncReport report = sync({QStringLiteral("CBZ1"), QStringLiteral("PDF1")});
        QCOMPARE(report.added, 1);
        QCOMPARE(report.skipped, 1);
        QCOMPARE(report.failed, 0);
    }

    QStringList kccArguments() const
    {
        QFile args(m_dir->filePath(QStringLiteral("kcc-args.txt")));
        return args.open(QIODevice::ReadOnly) ? QString::fromUtf8(args.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts) : QStringList();
    }

    // SPEC-COMICS CAC 13, 14
    void readingDirectionFollowsTheSeries()
    {
        setSeriesDirection(QStringLiteral("CBZ1"), QStringLiteral("RIGHT_TO_LEFT"));
        sync({QStringLiteral("CBZ1")});
        QVERIFY(kccArguments().contains(QStringLiteral("--manga-style")));
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("CBZ1"))->readingDirection, QStringLiteral("rtl"));

        setSeriesDirection(QStringLiteral("CBR1"), QStringLiteral("LEFT_TO_RIGHT"));
        sync({QStringLiteral("CBZ1"), QStringLiteral("CBR1")});
        QVERIFY(!kccArguments().contains(QStringLiteral("--manga-style")));
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("CBR1"))->readingDirection, QStringLiteral("ltr"));
    }

    // SPEC-COMICS CAC 15
    void directionChangeReconverts()
    {
        sync({QStringLiteral("CBZ1")});
        setSeriesDirection(QStringLiteral("CBZ1"), QStringLiteral("RIGHT_TO_LEFT"));
        const SyncReport report = sync({QStringLiteral("CBZ1")});
        QCOMPARE(report.updated, 1);
        QCOMPARE(report.plan.first().reason, QStringLiteral("reading direction changed"));
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("CBZ1"))->readingDirection, QStringLiteral("rtl"));
        QCOMPARE(sync({QStringLiteral("CBZ1")}).updated, 0);
    }

    // SPEC-COMICS CAC 15: an entry written before the field existed counts as left to right.
    void entryWithoutDirectionCountsAsLeftToRight()
    {
        sync({QStringLiteral("CBZ1")});
        Manifest manifest(m_device);
        QVERIFY(manifest.load());
        ManifestEntry entry = *manifest.findByBookId(QStringLiteral("CBZ1"));
        entry.readingDirection.clear();
        manifest.upsert(entry);
        QVERIFY(manifest.save());

        QCOMPARE(sync({QStringLiteral("CBZ1")}).updated, 0);
        setSeriesDirection(QStringLiteral("CBZ1"), QStringLiteral("RIGHT_TO_LEFT"));
        QCOMPARE(sync({QStringLiteral("CBZ1")}).updated, 1);
    }

    // SPEC-COMICS CAC 16
    void unreadableSeriesFailsTheComic()
    {
        m_server->setResponse("GET /api/v1/series/SER-CBZ1", {500, "{}"});
        const SyncReport report = sync({QStringLiteral("CBZ1"), QStringLiteral("EPUB1")});
        QCOMPARE(report.failed, 1);
        QCOMPARE(report.added, 1);
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("kobold/Author - Comic.kepub.epub"))));
    }

    void notFoundInKomgaIsSkippedNotFailed()
    {
        const SyncReport report = sync({QStringLiteral("MISSING")});
        QCOMPARE(report.skipped, 1);
        QCOMPARE(report.failed, 0);
    }

    void serverErrorCountsAsFailure()
    {
        m_server->setResponse("GET /api/v1/books/BROKEN", {500, "{}"});
        const SyncReport report = sync({QStringLiteral("BROKEN")});
        QCOMPARE(report.failed, 1);
        QCOMPARE(report.skipped, 0);
    }

    // AC 13
    void otherKomgaUrlStopsSync()
    {
        sync({QStringLiteral("EPUB1")});
        const SyncReport report = sync({}, false, QStringLiteral("https://other.example"));
        QCOMPARE(report.outcome, SyncReport::Outcome::KomgaUrlMismatch);
        QVERIFY(QFile::exists(devicePath(QStringLiteral("kobold/Author - First.kepub.epub"))));
        QCOMPARE(loadManifest().entries().size(), 1);
    }

    // AC 14
    void invalidManifestStopsSync()
    {
        sync({QStringLiteral("EPUB1")});
        {
            QFile manifest(devicePath(Manifest::relativeFilePath()));
            QVERIFY(manifest.open(QIODevice::WriteOnly));
            manifest.write("{broken");
        }
        const SyncReport report = sync({});
        QCOMPARE(report.outcome, SyncReport::Outcome::ManifestError);
        QVERIFY(QFile::exists(devicePath(QStringLiteral("kobold/Author - First.kepub.epub"))));
    }

    void renamedBookIsMovedWithoutDownload()
    {
        sync({QStringLiteral("EPUB1")});
        const int downloads = fileDownloads();
        m_server->setResponse(
            "GET /api/v1/books/EPUB1",
            {200, bookJson(QStringLiteral("EPUB1"), QStringLiteral("Renamed"), QStringLiteral("application/epub+zip"), QStringLiteral("h1"))});

        const SyncReport report = sync({QStringLiteral("EPUB1")});
        QCOMPARE(report.updated, 1);
        QCOMPARE(fileDownloads(), downloads);
        QVERIFY(!QFile::exists(devicePath(QStringLiteral("kobold/Author - First.kepub.epub"))));
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - Renamed.kepub.epub")), QByteArray("epub one"));
        QCOMPARE(loadManifest().findByBookId(QStringLiteral("EPUB1"))->path, QStringLiteral("kobold/Author - Renamed.kepub.epub"));
    }

    void caseOnlyTitleChangeKeepsFile()
    {
        // FAT treats both names as the same file; the Kobo shows the embedded title anyway.
        sync({QStringLiteral("EPUB1")});
        m_server->setResponse("GET /api/v1/books/EPUB1",
                              {200, bookJson(QStringLiteral("EPUB1"), QStringLiteral("FIRST"), QStringLiteral("application/epub+zip"), QStringLiteral("h1"))});
        const SyncReport report = sync({QStringLiteral("EPUB1")});
        QCOMPARE(report.updated, 0);
        QCOMPARE(report.failed, 0);
        QCOMPARE(readDeviceFile(QStringLiteral("kobold/Author - First.kepub.epub")), QByteArray("epub one"));
    }
};

QTEST_GUILESS_MAIN(SyncExecutorTest)

#include "syncexecutortest.moc"
