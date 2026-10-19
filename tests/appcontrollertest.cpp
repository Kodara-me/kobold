// SPDX-License-Identifier: MIT

#include "appcontroller.h"
#include "apikeystore.h"
#include "fakehttpserver.h"
#include "memorykeystore.h"
#include "kobobooksfixture.h"
#include "manifest.h"
#include "parentallock.h"
#include "shelfdatabase.h"
#include "selectionstore.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;

namespace
{
QByteArray page(const QJsonArray &content)
{
    return QJsonDocument(QJsonObject{{QLatin1String("content"), content}, {QLatin1String("last"), true}}).toJson(QJsonDocument::Compact);
}

QJsonObject series(const QString &id, const QString &title, const QString &libraryId)
{
    return QJsonObject{{QLatin1String("id"), id},
                       {QLatin1String("libraryId"), libraryId},
                       {QLatin1String("booksCount"), 3},
                       {QLatin1String("metadata"), QJsonObject{{QLatin1String("title"), title}}}};
}

QJsonObject book(const QString &id, const QString &seriesId, double number)
{
    return QJsonObject{{QLatin1String("id"), id},
                       {QLatin1String("seriesId"), seriesId},
                       {QLatin1String("seriesTitle"), seriesId},
                       {QLatin1String("media"),
                        QJsonObject{{QLatin1String("mediaType"), QStringLiteral("application/zip")}, {QLatin1String("status"), QStringLiteral("READY")}}},
                       {QLatin1String("metadata"), QJsonObject{{QLatin1String("title"), id}, {QLatin1String("numberSort"), number}}}};
}

void makeKobo(const QString &root, const QString &serial, int productId)
{
    QDir().mkpath(root + QStringLiteral("/.kobo"));
    QFile database(root + QStringLiteral("/.kobo/KoboReader.sqlite"));
    QFile version(root + QStringLiteral("/.kobo/version"));
    if (!database.open(QIODevice::WriteOnly) || !version.open(QIODevice::WriteOnly)) {
        qFatal("cannot create the fake Kobo");
    }
    version.write(QStringLiteral("%1,1,2,3,4,00000000-0000-0000-0000-000000000%2\n").arg(serial).arg(productId).toUtf8());
}
}

// Volumes in memory (SPEC-MOUNT.md): nothing real is ever mounted or ejected.
class FakeVolumes : public VolumeBackend
{
public:
    QList<KoboVolume> list;
    QStringList calls;

    QList<KoboVolume> volumes() const override
    {
        return list;
    }
    void mount(const QString &udi, Done done) override
    {
        calls.append(QStringLiteral("mount"));
        for (KoboVolume &volume : list) {
            if (volume.udi == udi) {
                volume.mounted = true;
            }
        }
        done(true, QString());
    }
    void unmount(const QString &, Done done) override
    {
        calls.append(QStringLiteral("unmount"));
        done(true, QString());
    }
    void powerOff(const QString &, Done done) override
    {
        calls.append(QStringLiteral("power off"));
        done(true, QString());
    }
};

class AppControllerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<FakeHttpServer> m_server;
    std::unique_ptr<QNetworkAccessManager> m_network;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<ApiKeyStore> m_apiKeyStore;
    std::unique_ptr<AppController> m_controller;

    bool hasSeriesTitled(const QString &title) const
    {
        return !seriesIdTitled(title).isEmpty();
    }

    QString seriesIdTitled(const QString &title) const
    {
        for (const QVariantMap &row : m_controller->series()->items()) {
            if (row.value(QStringLiteral("title")).toString() == title) {
                return row.value(QStringLiteral("id")).toString();
            }
        }
        return {};
    }

    static QVariantMap fieldRow(ShelfEditController *editor, const QString &field)
    {
        for (const QVariantMap &row : editor->fields()->items()) {
            if (row.value(QStringLiteral("field")).toString() == field) {
                return row;
            }
        }
        return {};
    }

    bool hasSeries(const QString &id) const
    {
        for (const QVariantMap &row : m_controller->series()->items()) {
            if (row.value(QStringLiteral("id")).toString() == id) {
                return true;
            }
        }
        return false;
    }

    QStringList bookStates()
    {
        QStringList states;
        for (const QVariantMap &row : m_controller->books()->items()) {
            states.append(row.value(QStringLiteral("selection")).toString());
        }
        return states;
    }

private Q_SLOTS:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();

        const QString media = m_dir.filePath(QStringLiteral("media"));
        QDir(media).removeRecursively();
        makeKobo(media + QStringLiteral("/KOBOeReader"), QStringLiteral("CLARA"), 386);
        qputenv("KOBOLD_MEDIA_ROOT", media.toUtf8());

        m_server = std::make_unique<FakeHttpServer>();
        m_server->setResponse("GET /api/v2/users/me", {200, R"({"id": "U", "email": "reader@example.com"})"});
        m_server->setResponse("GET /api/v1/libraries", {200, R"([{"id": "L1", "name": "Manga"}, {"id": "L2", "name": "Marvel"}])"});
        m_server->setResponse("POST /api/v1/series/list",
                              {200,
                               page({series(QStringLiteral("NARUTO"), QStringLiteral("Naruto"), QStringLiteral("L1")),
                                     series(QStringLiteral("AVENGERS"), QStringLiteral("Avengers"), QStringLiteral("L2"))})});
        m_server->setResponse("GET /api/v1/collections", {200, page({})});
        m_server->setResponse("GET /api/v1/readlists", {200, page({})});
        m_server->setResponse("GET /api/v1/series/NARUTO",
                              {200, QJsonDocument(series(QStringLiteral("NARUTO"), QStringLiteral("Naruto"), QStringLiteral("L1"))).toJson()});
        m_server->setResponse("GET /api/v1/books/N1", {200, QJsonDocument(book(QStringLiteral("N1"), QStringLiteral("NARUTO"), 1)).toJson()});
        m_server->setHandler("POST /api/v1/books/list", [](const FakeHttpServer::Request &) {
            return FakeHttpServer::Response{200,
                                            page({book(QStringLiteral("N1"), QStringLiteral("NARUTO"), 1),
                                                  book(QStringLiteral("N2"), QStringLiteral("NARUTO"), 2),
                                                  book(QStringLiteral("N3"), QStringLiteral("NARUTO"), 3)})};
        });

        QFile::remove(m_dir.filePath(QStringLiteral("koboldrc")));
        m_settings = std::make_unique<Settings>(KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig));
        QString error;
        QVERIFY(m_settings->setKomgaUrl(m_server->url().toString(), &error));
        m_network = std::make_unique<QNetworkAccessManager>();
        m_apiKeyStore = std::make_unique<ApiKeyStore>(QStringLiteral("kobold-test-unused"), QStringLiteral("unused"));
        m_controller = std::make_unique<AppController>(m_settings.get(), m_apiKeyStore.get(), m_network.get());

        m_controller->reloadKobos();
        m_controller->connectWithKey(QStringLiteral("key"));
        QTRY_COMPARE(m_controller->komgaState(), QStringLiteral("ready"));
        QTRY_COMPARE(m_controller->series()->rowCount(), 2);
    }

    void cleanup()
    {
        m_controller.reset();
        m_apiKeyStore.reset();
        m_network.reset();
        m_settings.reset();
        m_server.reset();
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    }

    // PAC 3, 5: adult series are hidden while locked, with the library left empty; nothing
    // about the selection changes.
    void parentalHidesAdultSeries()
    {
        QJsonObject adultByGenre = series(QStringLiteral("LANT"), QStringLiteral("Lanterns"), QStringLiteral("L3"));
        QJsonObject metadata = adultByGenre.value(QLatin1String("metadata")).toObject();
        metadata.insert(QLatin1String("genres"), QJsonArray{QStringLiteral("erotica & hentai")});
        adultByGenre.insert(QLatin1String("metadata"), metadata);
        QJsonObject adultByAge = series(QStringLiteral("BERSERK"), QStringLiteral("Berserk"), QStringLiteral("L1"));
        metadata = adultByAge.value(QLatin1String("metadata")).toObject();
        metadata.insert(QLatin1String("ageRating"), 18);
        adultByAge.insert(QLatin1String("metadata"), metadata);
        m_server->setResponse("GET /api/v1/libraries", {200, R"([{"id": "L1", "name": "Manga"}, {"id": "L3", "name": "Hentai"}])"});
        m_server->setResponse("POST /api/v1/series/list",
                              {200, page({series(QStringLiteral("NARUTO"), QStringLiteral("Naruto"), QStringLiteral("L1")), adultByGenre, adultByAge})});

        m_server->setResponse("GET /api/v1/readlists", {200, page({QJsonObject{{QLatin1String("id"), QStringLiteral("RL1")},
                                                                               {QLatin1String("name"), QStringLiteral("Late Night")},
                                                                               {QLatin1String("bookIds"), QJsonArray{QStringLiteral("Z1")}}}})});
        m_server->setResponse("GET /api/v1/readlists/RL1/books", {200, page({book(QStringLiteral("Z1"), QStringLiteral("LANT"), 1)})});

        QHash<QString, QString> keys;
        m_controller->setParentalPinStore(new MemoryKeyStore(&keys, ParentalLock::PinEntry, m_controller.get()));
        m_controller->reloadLibrary();
        QTRY_COMPARE(m_controller->series()->rowCount(), 3);

        const auto sidebarIds = [this]() {
            QStringList ids;
            for (const QVariantMap &row : m_controller->sidebar()->items()) {
                ids.append(row.value(QStringLiteral("id")).toString());
            }
            return ids;
        };
        QVERIFY(sidebarIds().contains(QStringLiteral("library:L3")));

        QSignalSpy result(m_controller.get(), &AppController::parentalResult);
        m_controller->enableParental(QStringLiteral("2468"));
        QTRY_COMPARE(result.count(), 1);
        QVERIFY(result.first().first().toBool());
        QVERIFY(m_controller->parentalLocked());

        QCOMPARE(m_controller->series()->rowCount(), 1);
        QVERIFY(!sidebarIds().contains(QStringLiteral("library:L3")));
        // A read list of adult books only is hidden too.
        QTRY_COMPARE(m_controller->readLists()->rowCount(), 0);
        QVERIFY(sidebarIds().contains(QStringLiteral("library:L1")));
        QVERIFY(!sidebarIds().contains(QStringLiteral("BERSERK")));
        const HiddenContent hidden = m_controller->hiddenContent();
        QVERIFY(hidden.seriesIds.contains(QStringLiteral("LANT")));
        QVERIFY(hidden.seriesIds.contains(QStringLiteral("BERSERK")));

        // An adult series cannot be opened while locked.
        m_controller->openItem(QStringLiteral("series"), QStringLiteral("LANT"));
        QVERIFY(m_controller->currentItem().value(QStringLiteral("id")).toString() != QLatin1String("LANT"));

        m_controller->unlockParental(QStringLiteral("2468"));
        QTRY_COMPARE(result.count(), 2);
        QVERIFY(!m_controller->parentalLocked());
        QCOMPARE(m_controller->series()->rowCount(), 3);
        QVERIFY(sidebarIds().contains(QStringLiteral("library:L3")));
        QVERIFY(m_controller->hiddenContent().isEmpty());
        QCOMPARE(m_controller->readLists()->rowCount(), 1);
    }

    // PAC 3: while locked, changing the selection never shows an adult entry, not even for the
    // moment Komga takes to answer.
    void lockedSelectionNeverFlashesAdultEntries()
    {
        QJsonObject adult = series(QStringLiteral("LANT"), QStringLiteral("Lanterns"), QStringLiteral("L1"));
        QJsonObject metadata = adult.value(QLatin1String("metadata")).toObject();
        metadata.insert(QLatin1String("genres"), QJsonArray{QStringLiteral("erotica & hentai")});
        adult.insert(QLatin1String("metadata"), metadata);
        m_server->setResponse("POST /api/v1/series/list", {200, page({series(QStringLiteral("NARUTO"), QStringLiteral("Naruto"), QStringLiteral("L1")), adult})});
        m_server->setResponse("GET /api/v1/series/LANT", {200, QJsonDocument(adult).toJson()});
        m_server->setResponse("GET /api/v1/books/Z1", {200, QJsonDocument(book(QStringLiteral("Z1"), QStringLiteral("LANT"), 1)).toJson()});
        m_server->setResponse("GET /api/v1/books/N3", {200, QJsonDocument(book(QStringLiteral("N3"), QStringLiteral("NARUTO"), 3)).toJson()});

        QHash<QString, QString> keys;
        m_controller->setParentalPinStore(new MemoryKeyStore(&keys, ParentalLock::PinEntry, m_controller.get()));
        m_controller->reloadLibrary();
        // The former list also has two series: wait for the new one.
        QTRY_VERIFY(hasSeries(QStringLiteral("LANT")));
        // An adult book and series already selected on this Kobo, before locking.
        QCOMPARE(m_controller->setBookSelected(QStringLiteral("Z1"), true), QString());
        QCOMPARE(m_controller->setSeriesFollowed(QStringLiteral("LANT"), true, QString()), QString());
        QTRY_COMPARE(m_controller->selectionGroups()->rowCount(), 1);
        QSignalSpy result(m_controller.get(), &AppController::parentalResult);
        m_controller->enableParental(QStringLiteral("2468"));
        QTRY_COMPARE(result.count(), 1);
        QVERIFY(m_controller->parentalLocked());
        QTRY_COMPARE(m_controller->hiddenSelectionCount(), 2);

        QStringList seen;
        const auto record = [this, &seen]() {
            for (const QVariantMap &row : m_controller->selectionBooks()->items()) {
                seen.append(row.value(QStringLiteral("id")).toString());
            }
            for (const QVariantMap &row : m_controller->selectionGroups()->items()) {
                seen.append(row.value(QStringLiteral("id")).toString());
            }
        };
        connect(m_controller->selectionBooks(), &QAbstractItemModel::modelReset, this, record);
        connect(m_controller->selectionGroups(), &QAbstractItemModel::modelReset, this, record);

        m_controller->openSeries(QStringLiteral("NARUTO"));
        QTRY_COMPARE(m_controller->books()->rowCount(), 3);
        QCOMPARE(m_controller->setBookSelected(QStringLiteral("N3"), true), QString());
        QTRY_COMPARE(m_controller->selectionBooks()->rowCount(), 1);
        QTRY_COMPARE(m_controller->hiddenSelectionCount(), 2);
        QVERIFY2(!seen.contains(QStringLiteral("Z1")), qPrintable(seen.join(QLatin1Char(' '))));
        QVERIFY2(!seen.contains(QStringLiteral("LANT")), qPrintable(seen.join(QLatin1Char(' '))));
        QVERIFY(seen.contains(QStringLiteral("N3")));
    }

    // SPEC-KOBO-BOOKS.md §6 (KBAC 1–3): the books of a Kobo, hidden then shown again; adult
    // ones left out while parental control is locked.
    void managesTheBooksOnAKobo()
    {
        const QString root = m_dir.filePath(QStringLiteral("media/KOBOeReader"));
        QFile::remove(root + QStringLiteral("/.kobo/KoboReader.sqlite"));
        createKoboBooksDatabase(root);
        QDir().mkpath(root + QStringLiteral("/Books"));
        QFile file(root + QStringLiteral("/Books/Other.epub"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        KoboBooksController *books = m_controller->koboBooks();
        books->open(QStringLiteral("CLARA"));
        QTRY_COMPARE(books->totalCount(), 3);
        QTRY_VERIFY(!books->busy());
        QCOMPARE(books->koboName(), m_controller->kobos()->get(0).value(QStringLiteral("name")).toString());

        books->setChecked(StoreBook, true);
        books->setChecked(OtherFile, true);
        QCOMPARE(books->checkedCount(), 2);
        QCOMPARE(books->preview(QStringLiteral("hide")), QStringLiteral("2 books will be hidden."));
        QCOMPARE(books->preview(QStringLiteral("show")), QString());
        books->apply(QStringLiteral("hide"));
        QTRY_COMPARE(books->hiddenCount(), 2);
        QTRY_VERIFY(!books->busy());
        QVERIFY2(books->errorString().isEmpty(), qPrintable(books->errorString()));
        QCOMPARE(books->checkedCount(), 0);
        QCOMPARE(books->history()->rowCount(), 1);
        QCOMPARE(books->backups()->rowCount(), 1);
        QVERIFY(QFile::exists(root + QStringLiteral("/.kobold/hidden/Books/Other.epub")));

        books->setFilterVisibility(QStringLiteral("hidden"));
        QCOMPARE(books->books()->rowCount(), 2);
        books->setFilterVisibility(QString());
        books->setFilterOrigin(QStringLiteral("store"));
        QCOMPARE(books->books()->rowCount(), 1);
        books->setFilterOrigin(QString());

        books->undo(books->history()->get(0).value(QStringLiteral("batch")).toString());
        QTRY_COMPARE(books->hiddenCount(), 0);
        QTRY_VERIFY(!books->busy());
        QCOMPARE(books->history()->rowCount(), 2);
        QVERIFY(books->history()->get(0).value(QStringLiteral("isUndo")).toBool());
        QVERIFY(books->history()->get(1).value(QStringLiteral("undone")).toBool());

        // The Komga book of an adult series is left out while locked.
        QJsonObject adult = series(QStringLiteral("LANT"), QStringLiteral("Lanterns"), QStringLiteral("L1"));
        QJsonObject metadata = adult.value(QLatin1String("metadata")).toObject();
        metadata.insert(QLatin1String("genres"), QJsonArray{QStringLiteral("erotica & hentai")});
        adult.insert(QLatin1String("metadata"), metadata);
        m_server->setResponse("POST /api/v1/series/list", {200, page({series(QStringLiteral("NARUTO"), QStringLiteral("Naruto"), QStringLiteral("L1")), adult})});
        QHash<QString, QString> keys;
        m_controller->setParentalPinStore(new MemoryKeyStore(&keys, ParentalLock::PinEntry, m_controller.get()));
        m_controller->reloadLibrary();
        // The former list also has two series: wait for the new one.
        QTRY_VERIFY(hasSeries(QStringLiteral("LANT")));
        QSignalSpy result(m_controller.get(), &AppController::parentalResult);
        m_controller->enableParental(QStringLiteral("2468"));
        QTRY_COMPARE(result.count(), 1);
        books->reload();
        QTRY_COMPARE(books->adultCount(), 1);
        QCOMPARE(books->totalCount(), 2);
    }

    // SPEC-KOBO-BOOKS.md §3.2: a Kobo connected with books it imported since the last sync gets
    // their series from the manifest, without a sync, and the user is told.
    void setsSeriesOnConnection()
    {
        const QString root = m_dir.filePath(QStringLiteral("media/KOBOeReader3"));
        makeKobo(root, QStringLiteral("SAGE"), 387);
        QFile::remove(root + QStringLiteral("/.kobo/KoboReader.sqlite"));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("series-connect"));
            db.setDatabaseName(root + QStringLiteral("/.kobo/KoboReader.sqlite"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE content (ContentID TEXT, ContentType TEXT, Title TEXT, Series TEXT, SeriesNumber TEXT, "
                                              "SeriesID TEXT, SeriesNumberFloat REAL)")));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO content VALUES ('file:///mnt/onboard/kobold/A - Asterix 3.kepub.epub', '6', 'Asterix 3', '', '', '', 0)")));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("series-connect"));
        Manifest manifest(root);
        QVERIFY(manifest.load());
        ManifestEntry entry;
        entry.komgaBookId = QStringLiteral("AST3");
        entry.path = QStringLiteral("kobold/A - Asterix 3.kepub.epub");
        entry.series = QStringLiteral("Astérix");
        entry.seriesNumber = 3;
        manifest.upsert(entry);
        QVERIFY(manifest.save());

        QSignalSpy told(m_controller.get(), &AppController::seriesSetOnConnect);
        m_controller->reloadKobos();
        QTRY_COMPARE_WITH_TIMEOUT(told.count(), 1, 10000);
        QVERIFY(told.first().at(0).toBool());
        QVERIFY2(told.first().at(1).toString().contains(QLatin1String("1 book")), qPrintable(told.first().at(1).toString()));
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("series-connect"));
            db.setDatabaseName(root + QStringLiteral("/.kobo/KoboReader.sqlite"));
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("SELECT SeriesID, SeriesNumber FROM content")) && query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("Astérix"));
            QCOMPARE(query.value(1).toString(), QStringLiteral("3"));
            db.close();
        }
        QSqlDatabase::removeDatabase(QStringLiteral("series-connect"));
        // Once per connection: reloading says nothing more.
        m_controller->reloadKobos();
        QTest::qWait(300);
        QCOMPARE(told.count(), 1);
        QDir(root).removeRecursively();
    }

    // A Kobo plugged in, then unplugged, shows up and goes by itself.
    void kobosRefreshByThemselves()
    {
        QCOMPARE(m_controller->kobos()->rowCount(), 1);
        const QString second = m_dir.filePath(QStringLiteral("media/KOBOeReader2"));
        makeKobo(second, QStringLiteral("ELIPSA"), 389);
        QTRY_COMPARE_WITH_TIMEOUT(m_controller->kobos()->rowCount(), 2, 10000);
        QDir(second).removeRecursively();
        QTRY_VERIFY_WITH_TIMEOUT(!m_controller->kobos()->get(1).value(QStringLiteral("connected")).toBool()
                                     || !m_controller->kobos()->get(0).value(QStringLiteral("connected")).toBool(),
                                 10000);
    }

    // SPEC-SHELF.md §6: a shelf shows as a library; its series opens like a Komga one.
    // SPEC-SHELF.md §2.1: one click makes the Kobold Library, with its place icon.
    void createsTheKoboldLibrary()
    {
        QTemporaryDir documents;
        auto database = std::make_unique<ShelfDatabase>(m_dir.filePath(QStringLiteral("shelves-library.sqlite")));
        QString error;
        QVERIFY(database->open(&error));
        m_controller->setShelfDatabase(std::move(database));
        m_controller->setKoboldLibraryParent(documents.path());
        QCOMPARE(m_controller->shelves()->rowCount(), 0);
        QCOMPARE(m_controller->koboldLibraryParent(), documents.path());

        QCOMPARE(m_controller->createKoboldLibrary(), QString());
        QTRY_VERIFY(!m_controller->shelfScanning());
        QCOMPARE(m_controller->shelves()->rowCount(), 1);
        QCOMPARE(m_controller->shelves()->get(0).value(QStringLiteral("name")).toString(), QStringLiteral("Kobold Library"));
        QFile directory(documents.filePath(QStringLiteral("Kobold Library/.directory")));
        QVERIFY(directory.open(QIODevice::ReadOnly));
        QVERIFY(directory.readAll().contains("Icon=folder-kobold"));
    }

    // SPEC-SHELF.md §4.3, §4.4 (SHAC 5, 6): editing a shelf book and series from the application,
    // and an online lookup that changes only the checked fields.
    void editsShelvesFromTheApplication()
    {
        QTemporaryDir shelfDir;
        QFile file(shelfDir.filePath(QStringLiteral("gift.pdf")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF");
        file.close();
        auto database = std::make_unique<ShelfDatabase>(m_dir.filePath(QStringLiteral("shelves-edit.sqlite")));
        QFile::remove(m_dir.filePath(QStringLiteral("shelves-edit.sqlite")));
        QString error;
        QVERIFY(database->open(&error));
        const auto shelf = database->addShelf(QStringLiteral("Home"), shelfDir.path(), &error);
        ShelfBook book;
        book.shelfId = shelf->id;
        book.path = QStringLiteral("gift.pdf");
        book.size = 4;
        book.modified = QDateTime::currentDateTimeUtc();
        book.fingerprint = QStringLiteral("f");
        book.mediaType = QStringLiteral("application/pdf");
        book.metadata.title = QStringLiteral("A Gift");
        book.metadata.series = QStringLiteral("Gifts");
        QVERIFY(database->saveBook(book));
        // Scanned already: no scan at start-up rewrites what the file says.
        database->setLastScan(shelf->id, QDateTime::currentDateTimeUtc());
        m_controller->setShelfDatabase(std::move(database));
        m_controller->reloadLibrary();
        QTRY_VERIFY(hasSeriesTitled(QStringLiteral("Gifts")));
        const QString bookId = ShelfLibrary::bookId(shelf->id, book.id);
        QCOMPARE(m_controller->seriesBookIds(seriesIdTitled(QStringLiteral("Gifts"))), QStringList({bookId}));

        ShelfEditController *editor = m_controller->shelfEditor();
        QCOMPARE(editor->openBooks({bookId}), QString());
        QCOMPARE(editor->kind(), QStringLiteral("book"));
        QCOMPARE(fieldRow(editor, QStringLiteral("title")).value(QStringLiteral("value")).toString(), QStringLiteral("A Gift"));
        QCOMPARE(fieldRow(editor, QStringLiteral("title")).value(QStringLiteral("source")).toString(), QStringLiteral("embedded"));

        QVERIFY(!editor->save({{QStringLiteral("ageRating"), QStringLiteral("old")}}).isEmpty());
        QCOMPARE(editor->save({{QStringLiteral("title"), QStringLiteral("The Gift")}}), QString());
        QCOMPARE(fieldRow(editor, QStringLiteral("title")).value(QStringLiteral("source")).toString(), QStringLiteral("manual"));
        QVERIFY(!m_controller->coverStamp().isEmpty());
        QCOMPARE(editor->clear(QStringLiteral("title")), QString());
        QCOMPARE(fieldRow(editor, QStringLiteral("title")).value(QStringLiteral("value")).toString(), QStringLiteral("A Gift"));

        const QString seriesId = seriesIdTitled(QStringLiteral("Gifts"));
        QCOMPARE(editor->openSeries(seriesId), QString());
        QCOMPARE(editor->save({{QStringLiteral("name"), QStringLiteral("Presents")}}), QString());
        QTRY_VERIFY(hasSeriesTitled(QStringLiteral("Presents")));
        QCOMPARE(seriesIdTitled(QStringLiteral("Presents")), seriesId);

        // Online: only the checked fields change; a field already set is not checked.
        m_server->setResponse("GET /search.json",
                              {200, R"({"docs": [{"key": "/works/OL1W", "title": "Gift Book", "author_name": ["Somebody"], "publisher": ["Viz"]}]})"});
        m_server->setResponse("GET /books/v1/volumes", {200, R"({"items": []})"});
        editor->setLookupBaseUrl(m_server->url());
        QCOMPARE(editor->openBooks({bookId}), QString());
        QCOMPARE(editor->lookupText(), QStringLiteral("A Gift"));
        editor->search(editor->lookupText());
        QTRY_COMPARE(editor->results()->rowCount(), 1);
        editor->choose(0);
        bool titleChecked = true;
        bool publisherChecked = false;
        for (const QVariantMap &row : editor->comparison()->items()) {
            if (row.value(QStringLiteral("field")).toString() == QLatin1String("title")) {
                titleChecked = row.value(QStringLiteral("checked")).toBool();
            }
            if (row.value(QStringLiteral("field")).toString() == QLatin1String("publisher")) {
                publisherChecked = row.value(QStringLiteral("checked")).toBool();
            }
        }
        QVERIFY(!titleChecked);
        QVERIFY(publisherChecked);
        QSignalSpy finished(editor, &ShelfEditController::lookupFinished);
        editor->applyLookup(false);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(finished.first().first().toBool());
        QCOMPARE(fieldRow(editor, QStringLiteral("publisher")).value(QStringLiteral("value")).toString(), QStringLiteral("Viz"));
        QCOMPARE(fieldRow(editor, QStringLiteral("publisher")).value(QStringLiteral("source")).toString(), QStringLiteral("online"));
        QCOMPARE(fieldRow(editor, QStringLiteral("title")).value(QStringLiteral("value")).toString(), QStringLiteral("A Gift"));

        // The ComicVine key goes to the key store, never elsewhere.
        QHash<QString, QString> keys;
        MemoryKeyStore store(&keys, ApiKeyStore::ComicVineKey);
        editor->setComicVineKeyStore(&store);
        editor->setComicVineKey(QStringLiteral(" CV-KEY "));
        QTRY_VERIFY(editor->hasComicVineKey());
        QCOMPARE(keys.value(ApiKeyStore::ComicVineKey), QStringLiteral("CV-KEY"));
        editor->setComicVineKeyStore(nullptr);
    }

    void shelvesShowNextToKomga()
    {
        QTemporaryDir shelfDir;
        QFile file(shelfDir.filePath(QStringLiteral("gift.pdf")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF");
        file.close();

        auto database = std::make_unique<ShelfDatabase>(m_dir.filePath(QStringLiteral("shelves-test.sqlite")));
        QString error;
        QVERIFY(database->open(&error));
        const auto shelf = database->addShelf(QStringLiteral("Home"), shelfDir.path(), &error);
        QVERIFY(shelf);
        ShelfBook book;
        book.shelfId = shelf->id;
        book.path = QStringLiteral("gift.pdf");
        book.size = 4;
        book.modified = QDateTime::currentDateTimeUtc();
        book.fingerprint = QStringLiteral("f");
        book.mediaType = QStringLiteral("application/pdf");
        book.metadata.title = QStringLiteral("A Gift");
        book.metadata.series = QStringLiteral("Gifts");
        book.metadata.number = 1;
        QVERIFY(database->saveBook(book));
        database->setLastScan(shelf->id, QDateTime::currentDateTimeUtc());
        m_controller->setShelfDatabase(std::move(database));
        m_controller->reloadLibrary();

        QTRY_VERIFY([this]() {
            for (const QVariantMap &row : m_controller->sidebar()->items()) {
                if (row.value(QStringLiteral("id")).toString() == QLatin1String("library:shelf:1")) {
                    return true;
                }
            }
            return false;
        }());
        QVERIFY(m_controller->hasShelves());
        QString seriesId;
        for (const QVariantMap &row : m_controller->series()->items()) {
            if (row.value(QStringLiteral("title")).toString() == QLatin1String("Gifts")) {
                seriesId = row.value(QStringLiteral("id")).toString();
            }
        }
        QVERIFY(seriesId.startsWith(QLatin1String("shelf:1:s")));
        m_controller->openItem(QStringLiteral("series"), seriesId);
        QTRY_COMPARE(m_controller->books()->rowCount(), 1);
        QCOMPARE(m_controller->books()->items().first().value(QStringLiteral("title")).toString(), QStringLiteral("A Gift"));
    }

    // MAC 1, 2: an unknown Kobo waiting to be mounted is mounted; the current Kobo is ejected.
    void mountsAndEjects()
    {
        FakeVolumes volumes;
        volumes.list = {KoboVolume{QStringLiteral("new"), QStringLiteral("BBBB"), false, m_dir.filePath(QStringLiteral("media/KOBOeReader"))},
                        KoboVolume{QStringLiteral("clara"), QStringLiteral("AAAA"), true, m_dir.filePath(QStringLiteral("media/KOBOeReader"))}};
        m_controller->setVolumeBackend(&volumes);
        QCOMPARE(m_controller->unknownUnmountedCount(), 1);

        QSignalSpy result(m_controller.get(), &AppController::mountResult);
        m_controller->mountKobo(QString());
        QTRY_COMPARE(result.count(), 1);
        QVERIFY(result.first().first().toBool());
        QCOMPARE(volumes.calls, QStringList{QStringLiteral("mount")});

        m_controller->ejectCurrentKobo();
        QTRY_COMPARE(result.count(), 2);
        QVERIFY(result.at(1).first().toBool());
        QCOMPARE(volumes.calls, (QStringList{QStringLiteral("mount"), QStringLiteral("unmount"), QStringLiteral("power off")}));
        m_controller->setVolumeBackend(nullptr);
    }

    // AAC 1: library and connected Kobo.
    void loadsLibraryAndKobo()
    {
        QCOMPARE(m_controller->userEmail(), QStringLiteral("reader@example.com"));
        QVERIFY(m_controller->multipleLibraries());
        QCOMPARE(m_controller->currentKobo(), QStringLiteral("CLARA"));
        QCOMPARE(m_controller->currentKoboName(), QStringLiteral("Clara 2E"));
        QVERIFY(m_controller->currentKoboConnected());
    }

    // AAC 2
    void filtersByLibraryAndName()
    {
        m_controller->setLibraryFilter(QStringLiteral("L1"));
        QCOMPARE(m_controller->series()->rowCount(), 1);
        QCOMPARE(m_controller->series()->get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Naruto"));
        m_controller->setLibraryFilter(QString());
        m_controller->setSeriesSearch(QStringLiteral("aveng"));
        QCOMPARE(m_controller->series()->rowCount(), 1);
        QCOMPARE(m_controller->series()->get(0).value(QStringLiteral("library")).toString(), QStringLiteral("Marvel"));
    }

    // AAC 3: same file as `kobold select add --series Naruto --volumes 1-2`.
    void followingSeriesWritesTheKoboSelection()
    {
        m_controller->openSeries(QStringLiteral("NARUTO"));
        QTRY_COMPARE(m_controller->books()->rowCount(), 3);
        // An invalid range is refused and changes nothing (AAC 4).
        QVERIFY(!m_controller->setSeriesFollowed(QStringLiteral("NARUTO"), true, QStringLiteral("2-1")).isEmpty());
        QVERIFY(!QFile::exists(SelectionStore::koboFilePath(QStringLiteral("CLARA")))
                || SelectionStore(SelectionStore::koboFilePath(QStringLiteral("CLARA"))).followedSeries().isEmpty());
        QVERIFY(m_controller->checkVolumes(QString()).isEmpty());

        QCOMPARE(m_controller->setSeriesFollowed(QStringLiteral("NARUTO"), true, QStringLiteral(" 1-2 ")), QString());
        SelectionStore store(SelectionStore::koboFilePath(QStringLiteral("CLARA")));
        QVERIFY(store.load());
        QCOMPARE(store.followedSeries(), QList<FollowedSeries>({{QStringLiteral("NARUTO"), QStringLiteral("1-2")}}));
        QVERIFY(store.bookIds().isEmpty());

        QTRY_COMPARE(bookStates(), QStringList({QStringLiteral("group"), QStringLiteral("group"), QStringLiteral("none")}));
        QTRY_COMPARE(m_controller->selectionCount(), 2);
        QCOMPARE(m_controller->series()->get(1).value(QStringLiteral("badge")).toString(), QStringLiteral("Following volumes 1-2"));
    }

    // AAC 4
    void checkingABookSelectsItDirectly()
    {
        m_controller->openSeries(QStringLiteral("NARUTO"));
        QTRY_COMPARE(m_controller->books()->rowCount(), 3);
        QCOMPARE(m_controller->setBookSelected(QStringLiteral("N3"), true), QString());
        QTRY_COMPARE(bookStates().last(), QStringLiteral("direct"));
        QTRY_COMPARE(m_controller->selectionBooks()->rowCount(), 1);
        QCOMPARE(m_controller->setBookSelected(QStringLiteral("N3"), false), QString());
        QTRY_COMPARE(bookStates().last(), QStringLiteral("none"));
    }

    // AAC 5
    void switchingKoboSwitchesSelection()
    {
        QCOMPARE(m_controller->setBookSelected(QStringLiteral("N1"), true), QString());
        makeKobo(qEnvironmentVariable("KOBOLD_MEDIA_ROOT") + QStringLiteral("/KOBOeReader1"), QStringLiteral("ELIPSA"), 389);
        m_controller->reloadKobos();
        QCOMPARE(m_controller->kobos()->rowCount(), 2);

        m_controller->setCurrentKobo(QStringLiteral("ELIPSA"));
        QCOMPARE(m_controller->currentKoboName(), QStringLiteral("Elipsa 2E"));
        QTRY_COMPARE(m_controller->selectionCount(), 0);
        QCOMPARE(m_settings->lastKobo(), QStringLiteral("ELIPSA"));

        SelectionStore clara(SelectionStore::koboFilePath(QStringLiteral("CLARA")));
        QVERIFY(clara.load());
        QCOMPARE(clara.bookIds(), QStringList({QStringLiteral("N1")}));
    }

    // AAC 7: settings follow the CLI rules.
    void settingsFollowCliRules()
    {
        QVERIFY(!m_controller->setKomgaUrl(QStringLiteral("ftp://x")).isEmpty());
        QVERIFY(!m_controller->renameKobo(QStringLiteral("CLARA"), QStringLiteral("a/b")).isEmpty());
        QCOMPARE(m_controller->renameKobo(QStringLiteral("CLARA"), QStringLiteral("Bedside")), QString());
        QCOMPARE(m_controller->currentKoboName(), QStringLiteral("Bedside"));
        m_controller->setComicProfile(QStringLiteral("CLARA"), QStringLiteral("KoL"));
        QCOMPARE(m_settings->comicProfileOverride(QStringLiteral("CLARA")), QStringLiteral("KoL"));
        QCOMPARE(m_controller->kobos()->get(0).value(QStringLiteral("profileSource")).toString(), QStringLiteral("override"));
        m_controller->setComicProfile(QStringLiteral("CLARA"), QString());
        QVERIFY(m_settings->comicProfileOverride(QStringLiteral("CLARA")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(AppControllerTest)

#include "appcontrollertest.moc"
