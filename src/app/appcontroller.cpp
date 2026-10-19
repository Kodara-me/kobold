// SPDX-License-Identifier: MIT

#include "appcontroller.h"
#include "kobobooks.h"
#include "shelffolder.h"

#include "apikeystore.h"
#include "bookformat.h"
#include "browsing.h"
#include "deviceidentity.h"
#include "diagnosticsrunner.h"
#include "kobotarget.h"
#include "localfiles.h"
#include "manifest.h"
#include "progressrunner.h"
#include "selectionstore.h"
#include "settings.h"
#include "syncexecutor.h"
#include "syncrunner.h"
#include "thumbnailcache.h"
#include "volumerange.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QIcon>
#include <QJSEngine>
#include <QLocale>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QTimer>
#include <QUrl>

namespace Kobold
{

namespace
{
AppController *s_instance = nullptr;

QString modelName(int productId)
{
    if (const auto model = koboModel(productId)) {
        return model->name;
    }
    return i18n("Unknown model");
}

// Why sync would skip this book, or empty.
QString skipReason(const KomgaBook &book)
{
    if (!isSupportedMediaType(book.mediaType)) {
        return book.mediaType.isEmpty() ? i18n("Not analyzed by Komga yet") : i18n("Unsupported format");
    }
    if (book.mediaStatus != MediaStatusReady) {
        return i18n("Not ready in Komga");
    }
    return {};
}

QString number(double value)
{
    return QString::number(value, 'g', 12);
}
}

AppController::AppController(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_apiKeyStore(apiKeyStore)
    , m_network(network)
    , m_client(new KomgaClient(network, this))
    , m_thumbnails(new ThumbnailCache(m_client, QString(), this))
    , m_kobos(new VariantListModel({"serial", "name", "model", "connected", "mountPoint", "profile", "profileSource", "koboSync", "koboSyncMessage", "backupBeforeSync"}, this))
    , m_libraries(new VariantListModel({"id", "name"}, this))
    , m_series(new VariantListModel({"id", "title", "library", "booksCount", "badge", "followed", "volumes"}, this))
    , m_collections(new VariantListModel({"id", "name", "count", "followed"}, this))
    , m_readLists(new VariantListModel({"id", "name", "count", "followed"}, this))
    , m_sidebar(new VariantListModel({"kind", "id", "title", "subtitle", "badge", "followed", "expanded", "current"}, this))
    , m_books(new VariantListModel({"id", "number", "title", "subtitle", "format", "selection", "groupName", "skipReason"}, this))
    , m_selectionGroups(new VariantListModel({"kind", "kindLabel", "id", "name", "volumes", "count"}, this))
    , m_selectionBooks(new VariantListModel({"id", "title", "format"}, this))
    , m_log(new VariantListModel({"kind", "message"}, this))
    , m_diagnostics(new VariantListModel({"level", "message", "hint"}, this))
{
    m_keptBooks = new VariantListModel({"bookId", "label", "reason"}, this);
    m_localFiles = new VariantListModel({"path", "name", "missing"}, this);
    m_shelves = new VariantListModel({"id", "name", "path", "books", "lastScan", "available"}, this);
    m_solidVolumes = std::make_unique<SolidVolumeBackend>();
    m_mounter = new KoboMounter(m_settings, m_solidVolumes.get(), this);
    KoboBooksController::Context booksContext;
    booksContext.mountPoint = [this](const QString &serial) {
        for (const QVariantMap &row : m_kobos->items()) {
            if (row.value(QStringLiteral("serial")).toString() == serial) {
                return row.value(QStringLiteral("mountPoint")).toString();
            }
        }
        return QString();
    };
    booksContext.koboName = [this](const QString &serial) {
        for (const QVariantMap &row : m_kobos->items()) {
            if (row.value(QStringLiteral("serial")).toString() == serial) {
                return row.value(QStringLiteral("name")).toString();
            }
        }
        return serial;
    };
    booksContext.adultFilter = [this]() {
        return koboBooksAdultFilter();
    };
    booksContext.operationRunning = [this]() {
        return m_busy || koboSyncBusy();
    };
    m_koboBooks = new KoboBooksController(booksContext, this);
    ShelfEditController::Context editContext;
    editContext.database = [this]() {
        return m_shelfDatabase.get();
    };
    editContext.library = [this]() {
        return m_shelfLibrary.get();
    };
    editContext.network = m_network;
    editContext.changed = [this](const QStringList &bookIds, const QStringList &seriesIds) {
        shelvesEdited(bookIds, seriesIds);
    };
    m_shelfEditor = new ShelfEditController(editContext, this);
    // Kobos plugged in, mounted, ejected or unplugged show up by themselves (SPEC-APP.md §6).
    m_kobosTimer = new QTimer(this);
    m_kobosTimer->setInterval(KobosRefreshMs);
    connect(m_kobosTimer, &QTimer::timeout, this, [this]() {
        if (kobosSignature() != m_kobosSignature) {
            reloadKobos();
        }
    });
    m_kobosTimer->start();
    auto shelfDatabase = std::make_unique<ShelfDatabase>();
    QString shelfError;
    if (shelfDatabase->open(&shelfError)) {
        setShelfDatabase(std::move(shelfDatabase));
    } else {
        qWarning() << shelfError;
    }
    m_collapsedSections = m_settings->collapsedSections();
    applyAppIcon(false);
    setParentalPinStore(new ApiKeyStore(ApiKeyStore::DefaultService, ParentalLock::PinEntry, this));
    // Any input counts as activity for the inactivity lock.
    QCoreApplication::instance()->installEventFilter(this);
}

AppController::~AppController() = default;

void AppController::setInstance(AppController *instance)
{
    s_instance = instance;
}

AppController *AppController::create(QQmlEngine *qmlEngine, QJSEngine *jsEngine)
{
    Q_UNUSED(qmlEngine)
    Q_UNUSED(jsEngine)
    QJSEngine::setObjectOwnership(s_instance, QJSEngine::CppOwnership);
    return s_instance;
}

ThumbnailCache *AppController::thumbnails() const
{
    return m_thumbnails;
}

// --- Komga connection ---------------------------------------------------------------

void AppController::start()
{
    reloadKobos();
    setKomgaState(QStringLiteral("connecting"), i18n("Connecting to %1…", komgaUrl()));
    m_apiKeyStore->read([this](const ApiKeyStore::Result &key) {
        switch (key.status) {
        case ApiKeyStore::Result::Status::Ok:
            connectWithKey(key.apiKey);
            return;
        case ApiKeyStore::Result::Status::NotFound:
            setKomgaState(QStringLiteral("noKey"), i18n("No Komga API key is set. Enter one in the settings."));
            mergeShelves();
            reloadSelection();
            return;
        case ApiKeyStore::Result::Status::Error:
            setKomgaState(QStringLiteral("error"), key.errorString);
            reloadSelection();
            return;
        }
    });
}

void AppController::connectWithKey(const QString &apiKey)
{
    m_client->setBaseUrl(m_settings->komgaUrl());
    m_client->setApiKey(apiKey);
    setKomgaState(QStringLiteral("connecting"), i18n("Connecting to %1…", komgaUrl()));
    m_client->fetchCurrentUser([this](const KomgaResult<KomgaUser> &user) {
        if (!user.ok()) {
            setKomgaState(QStringLiteral("error"), user.error.message);
            reloadSelection();
            return;
        }
        m_userEmail = user.value.email;
        setKomgaState(QStringLiteral("ready"), QString());
        reloadLibrary();
        reloadSelection();
    });
}

void AppController::setKomgaState(const QString &state, const QString &message)
{
    m_komgaState = state;
    m_komgaMessage = message;
    Q_EMIT komgaChanged();
}

QString AppController::komgaState() const
{
    return m_komgaState;
}

QString AppController::komgaMessage() const
{
    return m_komgaMessage;
}

QString AppController::komgaUrl() const
{
    return m_settings->komgaUrl().toString();
}

QString AppController::userEmail() const
{
    return m_userEmail;
}

// --- Kobos -------------------------------------------------------------------------------

QString AppController::kobosSignature() const
{
    return kobosSignature(connectedKobos(m_settings), m_mounter->unmounted());
}

QString AppController::kobosSignature(const QList<ConnectedKobo> &connected, const QList<UnmountedKobo> &unmounted)
{
    QStringList parts;
    for (const ConnectedKobo &device : connected) {
        parts.append(device.kobo.serial + QLatin1Char('@') + device.mountPoint);
    }
    for (const UnmountedKobo &item : unmounted) {
        parts.append(QStringLiteral("unmounted:") + item.udi);
    }
    parts.sort();
    return parts.join(QLatin1Char('|'));
}

VariantListModel *AppController::kobos() const
{
    return m_kobos;
}

void AppController::reloadKobos()
{
    const QList<ConnectedKobo> connected = connectedKobos(m_settings);
    m_mounter->rememberVolumes();
    m_unmounted = m_mounter->unmounted();
    const QList<KnownKobo> known = m_settings->knownKobos();
    m_kobosSignature = kobosSignature(connected, m_unmounted);

    QList<QVariantMap> rows;
    for (const KnownKobo &kobo : known) {
        QString mountPoint;
        for (const ConnectedKobo &device : connected) {
            if (device.kobo.serial == kobo.serial) {
                mountPoint = device.mountPoint;
            }
        }
        DeviceIdentity identity;
        identity.ok = true;
        identity.serial = kobo.serial;
        identity.productId = kobo.productId;
        const ComicProfileChoice profile = chooseComicProfile(identity, m_settings->comicProfileOverride(kobo.serial));
        rows.append({
            {QStringLiteral("serial"), kobo.serial},
            {QStringLiteral("name"), kobo.name},
            {QStringLiteral("model"), modelName(kobo.productId)},
            {QStringLiteral("connected"), !mountPoint.isEmpty()},
            {QStringLiteral("mountPoint"), mountPoint},
            {QStringLiteral("profile"), profile.profile},
            {QStringLiteral("profileSource"), profile.source},
            {QStringLiteral("koboSync"), m_settings->koboSyncInstalled(kobo.serial)},
            {QStringLiteral("koboSyncMessage"), m_koboSyncStatus.value(kobo.serial).message},
            {QStringLiteral("backupBeforeSync"), m_settings->backupBeforeSync(kobo.serial)},
        });
    }
    m_kobos->setItems(rows);

    // Check the Komga sync endpoint of every newly connected Kobo where it is installed.
    QSet<QString> connections;
    for (const ConnectedKobo &device : connected) {
        const QString connection = device.kobo.serial + QLatin1Char('@') + device.mountPoint;
        connections.insert(connection);
        if (m_settings->koboSyncInstalled(device.kobo.serial) && !m_koboSyncChecked.contains(connection)) {
            m_koboSyncChecked.insert(connection);
            checkKoboSync(device.kobo.serial);
        }
    }
    m_koboSyncChecked.intersect(connections);

    // A Kobo connected (or present at start-up) gets the series of the books it imported since
    // the last sync, without one (SPEC-KOBO-BOOKS.md §3.2). Not while it is in use: the sync
    // does it at its end anyway.
    if (m_settings->seriesOnConnect() && !m_busy && !m_koboBooks->busy()) {
        for (const ConnectedKobo &device : connected) {
            const QString connection = device.kobo.serial + QLatin1Char('@') + device.mountPoint;
            if (!m_seriesChecked.contains(connection)) {
                m_seriesChecked.insert(connection);
                setSeriesOnConnect(device.kobo.serial, device.kobo.name, device.mountPoint);
            }
        }
    }
    m_seriesChecked.intersect(connections);

    // Keep the current Kobo; otherwise the single connected one, the last used, the first.
    QString current = m_currentKobo;
    if (!m_settings->findKobo(current)) {
        current.clear();
    }
    if (current.isEmpty() && connected.size() == 1) {
        current = connected.first().kobo.serial;
    }
    if (current.isEmpty()) {
        const QString last = m_settings->lastKobo();
        if (m_settings->findKobo(last)) {
            current = last;
        }
    }
    if (current.isEmpty() && !known.isEmpty()) {
        current = known.first().serial;
    }

    const bool changed = current != m_currentKobo;
    m_currentKobo = current;
    m_currentKoboName.clear();
    m_currentKoboMount.clear();
    for (const QVariantMap &row : rows) {
        if (row.value(QStringLiteral("serial")).toString() == current) {
            m_currentKoboName = row.value(QStringLiteral("name")).toString();
            m_currentKoboMount = row.value(QStringLiteral("mountPoint")).toString();
        }
    }
    Q_EMIT currentKoboChanged();
    Q_EMIT koboSyncChanged();
    rebuildKeptBooks();
    if (changed) {
        reloadSelection();
    }
}

QString AppController::currentKobo() const
{
    return m_currentKobo;
}

void AppController::setCurrentKobo(const QString &serial)
{
    if (serial == m_currentKobo || !m_settings->findKobo(serial)) {
        return;
    }
    m_currentKobo = serial;
    m_settings->setLastKobo(serial);
    m_settings->sync();
    reloadKobos();
    reloadSelection();
}

QString AppController::currentKoboName() const
{
    return m_currentKoboName;
}

bool AppController::currentKoboConnected() const
{
    return !m_currentKoboMount.isEmpty();
}

// --- Library -------------------------------------------------------------------------

VariantListModel *AppController::libraries() const
{
    return m_libraries;
}

QString AppController::libraryFilter() const
{
    return m_libraryFilter;
}

void AppController::setLibraryFilter(const QString &libraryId)
{
    if (libraryId == m_libraryFilter) {
        return;
    }
    m_libraryFilter = libraryId;
    Q_EMIT filtersChanged();
    rebuildSeries();
    rebuildGroups();
}

QString AppController::seriesSearch() const
{
    return m_seriesSearch;
}

void AppController::setSeriesSearch(const QString &text)
{
    if (text == m_seriesSearch) {
        return;
    }
    m_seriesSearch = text;
    Q_EMIT filtersChanged();
    rebuildSeries();
    rebuildSidebar();
}

bool AppController::multipleLibraries() const
{
    return m_libraryList.size() > 1;
}

bool AppController::libraryLoading() const
{
    return m_libraryLoading;
}

VariantListModel *AppController::series() const
{
    return m_series;
}

VariantListModel *AppController::collections() const
{
    return m_collections;
}

VariantListModel *AppController::readLists() const
{
    return m_readLists;
}

QString AppController::libraryName(const QString &libraryId) const
{
    for (const KomgaLibrary &library : m_libraryList) {
        if (library.id == libraryId) {
            return library.name;
        }
    }
    return {};
}

void AppController::reloadLibrary()
{
    if (m_komgaState != QLatin1String("ready")) {
        return;
    }
    m_libraryLoading = true;
    Q_EMIT libraryChanged();

    auto pending = std::make_shared<int>(4);
    auto done = [this, pending]() {
        if (--*pending == 0) {
            m_libraryLoading = false;
            Q_EMIT libraryChanged();
            mergeShelves();
            m_readListSeriesLoaded = false;
            m_readListSeries.clear();
            loadReadListSeries();
        }
    };
    m_client->fetchLibraries([this, done](const KomgaResult<QList<KomgaLibrary>> &result) {
        m_komgaLibraries = Browsing::sortedByName(result.value, &KomgaLibrary::name);
        done();
    });
    m_client->fetchSeries(QString(), [this, done](const KomgaResult<QList<KomgaSeries>> &result) {
        m_komgaSeries = result.value;
        done();
    });
    m_client->fetchCollections(QString(), [this, done](const KomgaResult<QList<KomgaCollection>> &result) {
        m_allCollections = result.value;
        done();
    });
    m_client->fetchReadLists(QString(), [this, done](const KomgaResult<QList<KomgaReadList>> &result) {
        m_allReadLists = result.value;
        done();
    });
}

void AppController::refreshViews()
{
    rebuildSeries();
    rebuildGroups();
    rebuildSidebar();
    rebuildBooks();
    rebuildSelection();
    Q_EMIT currentItemChanged();
}

void AppController::rebuildSeries()
{
    QHash<QString, int> selectedPerSeries;
    for (const QString &bookId : std::as_const(m_resolved.bookIds)) {
        QString seriesId = m_directBooks.value(bookId).seriesId;
        if (const ResolvedGroup *group = m_resolved.groupOf(bookId)) {
            for (const KomgaBook &book : group->books) {
                if (book.id == bookId) {
                    seriesId = book.seriesId;
                }
            }
        }
        if (!seriesId.isEmpty()) {
            ++selectedPerSeries[seriesId];
        }
    }

    QList<QVariantMap> rows;
    for (const KomgaSeries &item : Browsing::sortedByName(m_allSeries, &KomgaSeries::title)) {
        if (!m_libraryFilter.isEmpty() && item.libraryId != m_libraryFilter) {
            continue;
        }
        if (hidesSeries(item.id)) {
            continue;
        }
        if (!m_seriesSearch.trimmed().isEmpty() && !item.title.contains(m_seriesSearch.trimmed(), Qt::CaseInsensitive)) {
            continue;
        }
        bool followed = false;
        QString volumes;
        if (m_store) {
            for (const FollowedSeries &entry : m_store->followedSeries()) {
                if (entry.id == item.id) {
                    followed = true;
                    volumes = entry.volumes;
                }
            }
        }
        QString badge;
        if (followed) {
            badge = volumes.isEmpty() ? i18n("Following") : i18n("Following volumes %1", volumes);
        } else if (selectedPerSeries.value(item.id) > 0) {
            badge = i18np("1 selected", "%1 selected", selectedPerSeries.value(item.id));
        }
        rows.append({
            {QStringLiteral("id"), item.id},
            {QStringLiteral("title"), item.title},
            {QStringLiteral("library"), libraryName(item.libraryId)},
            {QStringLiteral("booksCount"), item.booksCount},
            {QStringLiteral("badge"), badge},
            {QStringLiteral("followed"), followed},
            {QStringLiteral("volumes"), volumes},
        });
    }
    m_series->setItems(rows);
}

void AppController::rebuildGroups()
{
    const QStringList followedCollections = m_store ? m_store->followedCollections() : QStringList();
    const QStringList followedReadLists = m_store ? m_store->followedReadLists() : QStringList();

    QList<QVariantMap> collections;
    for (const KomgaCollection &item : Browsing::sortedByName(m_allCollections, &KomgaCollection::name)) {
        // A collection whose every series is hidden is hidden too (SPEC-PARENTAL.md §4).
        qsizetype visible = 0;
        for (const QString &seriesId : item.seriesIds) {
            visible += hidesSeries(seriesId) ? 0 : 1;
        }
        if (visible == 0 && !item.seriesIds.isEmpty()) {
            continue;
        }
        collections.append({{QStringLiteral("id"), item.id},
                            {QStringLiteral("name"), item.name},
                            {QStringLiteral("count"), i18np("1 series", "%1 series", visible)},
                            {QStringLiteral("followed"), followedCollections.contains(item.id)}});
    }
    m_collections->setItems(collections);

    QList<QVariantMap> readLists;
    for (const KomgaReadList &item : Browsing::sortedByName(m_allReadLists, &KomgaReadList::name)) {
        // A read list whose every book is hidden is hidden too (SPEC-PARENTAL.md §4).
        const QSet<QString> series = m_readListSeries.value(item.id);
        if (!series.isEmpty() && std::all_of(series.cbegin(), series.cend(), [this](const QString &id) {
                return hidesSeries(id);
            })) {
            continue;
        }
        readLists.append({{QStringLiteral("id"), item.id},
                          {QStringLiteral("name"), item.name},
                          {QStringLiteral("count"), i18np("1 book", "%1 books", item.bookIds.size())},
                          {QStringLiteral("followed"), followedReadLists.contains(item.id)}});
    }
    m_readLists->setItems(readLists);
}

// --- Sidebar and center pane ------------------------------------------------------

VariantListModel *AppController::sidebar() const
{
    return m_sidebar;
}

void AppController::toggleSection(const QString &sectionId)
{
    if (m_collapsedSections.contains(sectionId)) {
        m_collapsedSections.removeAll(sectionId);
    } else {
        m_collapsedSections.append(sectionId);
    }
    m_settings->setCollapsedSections(m_collapsedSections);
    m_settings->sync();
    rebuildSidebar();
}

void AppController::rebuildSidebar()
{
    const QString search = m_seriesSearch.trimmed();
    const bool searching = !search.isEmpty();
    QList<QVariantMap> rows;
    const auto addSection = [this, &rows, searching](const QString &sectionId, const QString &title, const QList<QVariantMap> &items) {
        if (items.isEmpty() && searching) {
            return;
        }
        const bool expanded = searching || !m_collapsedSections.contains(sectionId);
        rows.append({{QStringLiteral("kind"), QStringLiteral("header")},
                     {QStringLiteral("id"), sectionId},
                     {QStringLiteral("title"), title},
                     {QStringLiteral("subtitle"), QString::number(items.size())},
                     {QStringLiteral("badge"), QString()},
                     {QStringLiteral("followed"), false},
                     {QStringLiteral("expanded"), expanded},
                     {QStringLiteral("current"), false}});
        if (expanded) {
            rows.append(items);
        }
    };

    // Series rows come from the series model, already filtered by the search and badged.
    QHash<QString, QList<QVariantMap>> seriesByLibrary;
    for (const QVariantMap &series : m_series->items()) {
        QString libraryId;
        for (const KomgaSeries &item : std::as_const(m_allSeries)) {
            if (item.id == series.value(QStringLiteral("id")).toString()) {
                libraryId = item.libraryId;
            }
        }
        const QString id = series.value(QStringLiteral("id")).toString();
        seriesByLibrary[libraryId].append({{QStringLiteral("kind"), QStringLiteral("series")},
                                           {QStringLiteral("id"), id},
                                           {QStringLiteral("title"), series.value(QStringLiteral("title"))},
                                           {QStringLiteral("subtitle"), i18np("1 book", "%1 books", series.value(QStringLiteral("booksCount")).toInt())},
                                           {QStringLiteral("badge"), series.value(QStringLiteral("badge"))},
                                           {QStringLiteral("followed"), series.value(QStringLiteral("followed"))},
                                           {QStringLiteral("expanded"), false},
                                           {QStringLiteral("current"), m_currentKind == QLatin1String("series") && m_currentId == id}});
    }
    // Libraries left with nothing to show while adult content is locked are hidden.
    QSet<QString> librariesWithHidden;
    for (const KomgaSeries &item : std::as_const(m_allSeries)) {
        if (hidesSeries(item.id)) {
            librariesWithHidden.insert(item.libraryId);
        }
    }
    for (const KomgaLibrary &library : std::as_const(m_libraryList)) {
        const QList<QVariantMap> items = seriesByLibrary.value(library.id);
        if (items.isEmpty() && librariesWithHidden.contains(library.id)) {
            continue;
        }
        addSection(QStringLiteral("library:") + library.id, library.name, items);
    }

    if (!searching) {
        const auto groupRows = [this](VariantListModel *model, const QString &kind) {
            QList<QVariantMap> items;
            for (const QVariantMap &group : model->items()) {
                const QString id = group.value(QStringLiteral("id")).toString();
                items.append({{QStringLiteral("kind"), kind},
                              {QStringLiteral("id"), id},
                              {QStringLiteral("title"), group.value(QStringLiteral("name"))},
                              {QStringLiteral("subtitle"), group.value(QStringLiteral("count"))},
                              {QStringLiteral("badge"), group.value(QStringLiteral("followed")).toBool() ? i18n("Following") : QString()},
                              {QStringLiteral("followed"), group.value(QStringLiteral("followed"))},
                              {QStringLiteral("expanded"), false},
                              {QStringLiteral("current"), m_currentKind == kind && m_currentId == id}});
            }
            return items;
        };
        addSection(QStringLiteral("collections"), i18n("Collections"), groupRows(m_collections, QStringLiteral("collection")));
        addSection(QStringLiteral("readlists"), i18n("Read Lists"), groupRows(m_readLists, QStringLiteral("readlist")));
    }
    m_sidebar->setItems(rows);
}

VariantListModel *AppController::books() const
{
    return m_books;
}

bool AppController::booksLoading() const
{
    return m_booksLoading;
}

QVariantMap AppController::currentItem() const
{
    QVariantMap item{{QStringLiteral("kind"), m_currentKind}, {QStringLiteral("id"), m_currentId}};
    if (m_currentKind == QLatin1String("series")) {
        bool followed = false;
        QString volumes;
        if (m_store) {
            for (const FollowedSeries &entry : m_store->followedSeries()) {
                if (entry.id == m_currentSeries.id) {
                    followed = true;
                    volumes = entry.volumes;
                }
            }
        }
        QStringList details;
        if (multipleLibraries()) {
            details.append(libraryName(m_currentSeries.libraryId));
        }
        details.append(i18np("1 book", "%1 books", m_currentSeries.booksCount));
        if (m_currentSeries.isRightToLeft()) {
            details.append(i18n("Read right to left"));
        }
        item.insert(QStringLiteral("title"), m_currentSeries.title);
        item.insert(QStringLiteral("details"), details.join(QStringLiteral(" · ")));
        item.insert(QStringLiteral("cover"), QString(QStringLiteral("image://komga/series/") + m_currentSeries.id));
        item.insert(QStringLiteral("followed"), followed);
        item.insert(QStringLiteral("volumes"), volumes);
    } else if (m_currentKind == QLatin1String("collection")) {
        item.insert(QStringLiteral("title"), m_currentCollection.name);
        item.insert(QStringLiteral("details"), i18n("Collection · %1", i18np("1 series", "%1 series", m_currentCollection.seriesIds.size())));
        item.insert(QStringLiteral("followed"), m_store && m_store->followedCollections().contains(m_currentId));
    } else if (m_currentKind == QLatin1String("readlist")) {
        item.insert(QStringLiteral("title"), m_currentReadList.name);
        item.insert(QStringLiteral("details"), i18n("Read list · %1", i18np("1 book", "%1 books", m_currentReadList.bookIds.size())));
        item.insert(QStringLiteral("followed"), m_store && m_store->followedReadLists().contains(m_currentId));
    }
    return item;
}

void AppController::openItem(const QString &kind, const QString &id)
{
    if (kind == QLatin1String("series") && hidesSeries(id)) {
        return;
    }
    const int generation = ++m_openGeneration;
    m_currentKind = kind;
    m_currentId = id;
    m_seriesBooks.clear();
    m_books->clear();
    m_booksLoading = true;
    rebuildSidebar();

    if (kind == QLatin1String("series")) {
        m_currentSeries = KomgaSeries();
        m_currentSeries.id = id;
        for (const KomgaSeries &item : std::as_const(m_allSeries)) {
            if (item.id == id) {
                m_currentSeries = item;
            }
        }
        Q_EMIT currentItemChanged();
        m_client->fetchOneSeries(id, [this, generation](const KomgaResult<KomgaSeries> &series) {
            if (series.ok() && generation == m_openGeneration) {
                m_currentSeries = series.value;
                Q_EMIT currentItemChanged();
            }
        });
        m_client->fetchSeriesBooks(id, [this, generation](const KomgaResult<QList<KomgaBook>> &books) {
            if (generation != m_openGeneration) {
                return;
            }
            m_seriesBooks = Browsing::sortSeriesBooks(books.value);
            m_booksLoading = false;
            rebuildBooks();
            Q_EMIT currentItemChanged();
        });
        return;
    }

    if (kind == QLatin1String("collection")) {
        m_currentCollection = KomgaCollection();
        for (const KomgaCollection &item : std::as_const(m_allCollections)) {
            if (item.id == id) {
                m_currentCollection = item;
            }
        }
        Q_EMIT currentItemChanged();
        m_client->fetchCollectionSeries(id, QString(), [this, generation](const KomgaResult<QList<KomgaSeries>> &series) {
            if (generation != m_openGeneration) {
                return;
            }
            collectCollectionBooks(generation, Browsing::orderCollectionSeries(m_currentCollection, series.value), 0);
        });
        return;
    }

    m_currentReadList = KomgaReadList();
    for (const KomgaReadList &item : std::as_const(m_allReadLists)) {
        if (item.id == id) {
            m_currentReadList = item;
        }
    }
    Q_EMIT currentItemChanged();
    m_client->fetchReadListBooks(id, QString(), [this, generation](const KomgaResult<QList<KomgaBook>> &books) {
        if (generation != m_openGeneration) {
            return;
        }
        m_seriesBooks = Browsing::orderReadListBooks(m_currentReadList, books.value);
        m_booksLoading = false;
        rebuildBooks();
        Q_EMIT currentItemChanged();
    });
}

void AppController::collectCollectionBooks(int generation, QList<KomgaSeries> series, qsizetype index)
{
    if (generation != m_openGeneration) {
        return;
    }
    if (index >= series.size()) {
        m_booksLoading = false;
        rebuildBooks();
        Q_EMIT currentItemChanged();
        return;
    }
    m_client->fetchSeriesBooks(series.at(index).id, [this, generation, series, index](const KomgaResult<QList<KomgaBook>> &books) {
        if (generation != m_openGeneration) {
            return;
        }
        m_seriesBooks.append(Browsing::sortSeriesBooks(books.value));
        collectCollectionBooks(generation, series, index + 1);
    });
}

void AppController::openSeries(const QString &seriesId)
{
    openItem(QStringLiteral("series"), seriesId);
}

QString AppController::setCurrentItemFollowed(bool followed, const QString &volumes)
{
    if (m_currentKind == QLatin1String("series")) {
        return setSeriesFollowed(m_currentId, followed, volumes);
    }
    if (m_currentKind == QLatin1String("collection")) {
        return setCollectionFollowed(m_currentId, followed);
    }
    if (m_currentKind == QLatin1String("readlist")) {
        return setReadListFollowed(m_currentId, followed);
    }
    return {};
}

void AppController::rebuildBooks()
{
    const bool showSeries = m_currentKind != QLatin1String("series");
    QList<QVariantMap> rows;
    for (const KomgaBook &book : std::as_const(m_seriesBooks)) {
        if (hidesSeries(book.seriesId)) {
            continue;
        }
        QString state = QStringLiteral("none");
        QString groupName;
        if (m_store && m_store->contains(book.id)) {
            state = QStringLiteral("direct");
        } else if (const ResolvedGroup *group = m_resolved.groupOf(book.id)) {
            state = QStringLiteral("group");
            groupName = group->description();
        }
        QString reason = skipReason(book);
        if (reason.isEmpty() && book.mediaType == MediaTypeEpub && currentKoboSyncInstalled()) {
            reason = i18n("Synced by Komga");
        }
        QString subtitle;
        if (state == QLatin1String("group")) {
            subtitle = i18n("Selected through the followed %1", groupName);
        } else if (!reason.isEmpty()) {
            subtitle = reason;
        } else {
            subtitle = i18nc("@info volume number · format", "Volume %1 · %2", number(book.numberSort), formatName(book));
        }
        if (showSeries && !book.seriesTitle.isEmpty()) {
            subtitle = book.seriesTitle + QStringLiteral(" · ") + subtitle;
        }
        rows.append({
            {QStringLiteral("id"), book.id},
            {QStringLiteral("number"), number(book.numberSort)},
            {QStringLiteral("title"), book.title.isEmpty() ? book.name : book.title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("format"), formatName(book)},
            {QStringLiteral("selection"), state},
            {QStringLiteral("groupName"), groupName},
            {QStringLiteral("skipReason"), reason},
        });
    }
    m_books->setItems(rows);
}

// --- Selection -------------------------------------------------------------------------

QString AppController::checkVolumes(const QString &volumes) const
{
    if (volumes.trimmed().isEmpty() || VolumeRange::parse(volumes)) {
        return {};
    }
    return i18n("Invalid volume range; examples: 1-10, 5, 1-3,7,10-");
}

void AppController::reloadSelection()
{
    const int generation = ++m_selectionGeneration;
    // Book metadata stays valid across reloads, and the groups of the same Kobo stay shown
    // until resolved again: no flash of ids, and nothing unfiltered (SPEC-PARENTAL.md).
    const QList<ResolvedGroup> previousGroups = m_resolvedKobo == m_currentKobo ? m_resolved.groups : QList<ResolvedGroup>();
    m_resolved = ResolvedSelection();
    m_resolved.groups = previousGroups;
    m_selectionPending = false;
    m_selectionError.clear();
    m_store.reset();

    if (m_currentKobo.isEmpty()) {
        m_selectionError = i18n("No Kobo known yet. Plug one in.");
        refreshViews();
        Q_EMIT selectionChanged();
        return;
    }
    m_store = std::make_unique<SelectionStore>(SelectionStore::koboFilePath(m_currentKobo));
    if (!m_store->load()) {
        m_selectionError = m_store->errorString();
        m_store.reset();
        refreshViews();
        Q_EMIT selectionChanged();
        return;
    }
    m_resolved.directBookIds = m_store->bookIds();
    m_resolved.bookIds = m_store->bookIds();
    refreshViews();
    Q_EMIT selectionChanged();

    if (m_komgaState != QLatin1String("ready")) {
        return;
    }
    m_selectionPending = true;
    const QString kobo = m_currentKobo;
    resolveSelection(m_client, *m_store, [this, generation, kobo](const ResolvedSelection &resolved) {
        if (generation != m_selectionGeneration) {
            return;
        }
        m_resolved = resolved;
        m_resolvedKobo = kobo;
        if (!resolved.ok()) {
            m_selectionError = resolved.errorString;
        }
        const QStringList direct = m_store ? m_store->bookIds() : QStringList();
        auto pending = std::make_shared<qsizetype>(direct.size());
        if (direct.isEmpty()) {
            m_selectionPending = false;
            refreshViews();
            Q_EMIT selectionChanged();
            return;
        }
        for (const QString &bookId : direct) {
            m_client->fetchBook(bookId, [this, generation, pending](const KomgaResult<KomgaBook> &book) {
                if (generation != m_selectionGeneration) {
                    return;
                }
                if (book.ok()) {
                    m_directBooks.insert(book.value.id, book.value);
                }
                if (--*pending == 0) {
                    m_selectionPending = false;
                    refreshViews();
                    Q_EMIT selectionChanged();
                }
            });
        }
    });
}

void AppController::rebuildSelection()
{
    m_hiddenSelectionCount = 0;
    QList<QVariantMap> groups;
    for (const ResolvedGroup &group : std::as_const(m_resolved.groups)) {
        if (group.kind == ResolvedGroup::Kind::Series && hidesSeries(group.id)) {
            ++m_hiddenSelectionCount;
            continue;
        }
        QString kind;
        QString kindLabel;
        switch (group.kind) {
        case ResolvedGroup::Kind::Series:
            kind = QStringLiteral("series");
            kindLabel = i18n("Series");
            break;
        case ResolvedGroup::Kind::Collection:
            kind = QStringLiteral("collection");
            kindLabel = i18n("Collection");
            break;
        case ResolvedGroup::Kind::ReadList:
            kind = QStringLiteral("readlist");
            kindLabel = i18n("Read list");
            break;
        }
        groups.append({{QStringLiteral("kind"), kind},
                       {QStringLiteral("kindLabel"), kindLabel},
                       {QStringLiteral("id"), group.id},
                       {QStringLiteral("name"), group.name},
                       {QStringLiteral("volumes"), group.volumes},
                       {QStringLiteral("count"), int(group.books.size())}});
    }
    // Before resolution (or without Komga), followed groups are listed by id.
    if (m_resolved.groups.isEmpty() && m_store) {
        for (const FollowedSeries &series : m_store->followedSeries()) {
            if (hidesSeries(series.id)) {
                ++m_hiddenSelectionCount;
                continue;
            }
            groups.append({{QStringLiteral("kind"), QStringLiteral("series")},
                           {QStringLiteral("kindLabel"), i18n("Series")},
                           {QStringLiteral("id"), series.id},
                           {QStringLiteral("name"), series.id},
                           {QStringLiteral("volumes"), series.volumes},
                           {QStringLiteral("count"), -1}});
        }
        for (const QString &id : m_store->followedCollections()) {
            groups.append({{QStringLiteral("kind"), QStringLiteral("collection")},
                           {QStringLiteral("kindLabel"), i18n("Collection")},
                           {QStringLiteral("id"), id},
                           {QStringLiteral("name"), id},
                           {QStringLiteral("volumes"), QString()},
                           {QStringLiteral("count"), -1}});
        }
        for (const QString &id : m_store->followedReadLists()) {
            groups.append({{QStringLiteral("kind"), QStringLiteral("readlist")},
                           {QStringLiteral("kindLabel"), i18n("Read list")},
                           {QStringLiteral("id"), id},
                           {QStringLiteral("name"), id},
                           {QStringLiteral("volumes"), QString()},
                           {QStringLiteral("count"), -1}});
        }
    }
    m_selectionGroups->setItems(groups);

    QList<QVariantMap> books;
    const QStringList direct = m_store ? m_store->bookIds() : QStringList();
    for (const QString &bookId : direct) {
        const KomgaBook book = m_directBooks.value(bookId);
        if (hidesSeries(book.seriesId)) {
            ++m_hiddenSelectionCount;
            continue;
        }
        // Its series is not known yet: it shows once fetched, if it may.
        if (book.id.isEmpty() && m_selectionPending && !m_adultSeries.isEmpty()) {
            continue;
        }
        books.append({{QStringLiteral("id"), bookId},
                      {QStringLiteral("title"), book.id.isEmpty() ? bookId : bookLabel(book)},
                      {QStringLiteral("format"), book.id.isEmpty() ? QString() : formatName(book)}});
    }
    m_selectionBooks->setItems(books);

    QList<QVariantMap> files;
    for (const QString &path : m_store ? m_store->localFiles() : QStringList()) {
        files.append({{QStringLiteral("path"), path},
                      {QStringLiteral("name"), QFileInfo(path).fileName()},
                      {QStringLiteral("missing"), !QFileInfo::exists(path)}});
    }
    m_localFiles->setItems(files);
}

VariantListModel *AppController::localFiles() const
{
    return m_localFiles;
}

QString AppController::addLocalFiles(const QList<QUrl> &files)
{
    if (!m_store) {
        return saveSelection();
    }
    QStringList refused;
    for (const QUrl &url : files) {
        const QString path = url.toLocalFile();
        if (!QFileInfo(path).isFile() || LocalFiles::mediaTypeFor(path).isEmpty()) {
            refused.append(QFileInfo(path).fileName());
            continue;
        }
        m_store->addLocalFile(QFileInfo(path).absoluteFilePath());
    }
    const QString error = saveSelection();
    if (!error.isEmpty()) {
        return error;
    }
    return refused.isEmpty() ? QString() : i18n("Not an EPUB, PDF, CBZ or CBR file: %1", refused.join(QLatin1String(", ")));
}

QString AppController::removeLocalFile(const QString &path)
{
    if (!m_store) {
        return saveSelection();
    }
    m_store->removeLocalFile(path);
    return saveSelection();
}

QString AppController::saveSelection()
{
    if (!m_store) {
        return m_selectionError.isEmpty() ? i18n("No Kobo selected") : m_selectionError;
    }
    if (!m_store->save()) {
        return m_store->errorString();
    }
    reloadSelection();
    return {};
}

QString AppController::setSeriesFollowed(const QString &seriesId, bool followed, const QString &volumes)
{
    if (!m_store) {
        return saveSelection();
    }
    if (!followed) {
        m_store->unfollowSeries(seriesId);
        return saveSelection();
    }
    QString normalized;
    if (!volumes.trimmed().isEmpty()) {
        const auto range = VolumeRange::parse(volumes);
        if (!range) {
            return checkVolumes(volumes);
        }
        normalized = range->toString();
    }
    m_store->followSeries(seriesId, normalized);
    return saveSelection();
}

QString AppController::setBookSelected(const QString &bookId, bool selected)
{
    if (!m_store) {
        return saveSelection();
    }
    if (selected) {
        m_store->add(bookId);
    } else {
        m_store->remove(bookId);
    }
    return saveSelection();
}

QString AppController::setCollectionFollowed(const QString &collectionId, bool followed)
{
    if (!m_store) {
        return saveSelection();
    }
    followed ? m_store->followCollection(collectionId) : m_store->unfollowCollection(collectionId);
    return saveSelection();
}

QString AppController::setReadListFollowed(const QString &readListId, bool followed)
{
    if (!m_store) {
        return saveSelection();
    }
    followed ? m_store->followReadList(readListId) : m_store->unfollowReadList(readListId);
    return saveSelection();
}

QString AppController::removeFromSelection(const QString &kind, const QString &id)
{
    if (kind == QLatin1String("series")) {
        return setSeriesFollowed(id, false);
    }
    if (kind == QLatin1String("collection")) {
        return setCollectionFollowed(id, false);
    }
    if (kind == QLatin1String("readlist")) {
        return setReadListFollowed(id, false);
    }
    return setBookSelected(id, false);
}

VariantListModel *AppController::selectionGroups() const
{
    return m_selectionGroups;
}

VariantListModel *AppController::selectionBooks() const
{
    return m_selectionBooks;
}

int AppController::selectionCount() const
{
    return int(m_resolved.bookIds.size());
}

QString AppController::selectionError() const
{
    return m_selectionError;
}

// --- Operations ----------------------------------------------------------------------

bool AppController::busy() const
{
    return m_busy;
}

VariantListModel *AppController::log() const
{
    return m_log;
}

QStringList AppController::failures() const
{
    return m_failures;
}

int AppController::lastExitCode() const
{
    return m_lastExitCode;
}

bool AppController::lastSyncWrote() const
{
    return m_lastSyncWrote;
}

void AppController::resetProgress()
{
    m_currentStep.clear();
    m_progressDone = 0;
    m_progressTotal = 0;
    Q_EMIT progressChanged();
}

void AppController::runOperation(Runner *runner, std::function<void(Runner *)> start)
{
    connect(runner, &Runner::output, this, [this](Runner::OutputKind kind, const QString &text) {
        const QString kindName = kind == Runner::OutputKind::Error ? QStringLiteral("error")
            : kind == Runner::OutputKind::Warning                  ? QStringLiteral("warning")
                                                                   : QStringLiteral("line");
        m_log->append({{QStringLiteral("kind"), kindName}, {QStringLiteral("message"), text}});
        m_currentStep = text.trimmed();
        Q_EMIT progressChanged();
        if (kind == Runner::OutputKind::Error) {
            m_failures.append(text);
        }
    });
    start(runner);
}

// --- Komga Kobo sync ----------------------------------------------------------------------

bool AppController::currentKoboSyncInstalled() const
{
    return !m_currentKobo.isEmpty() && m_settings->koboSyncInstalled(m_currentKobo);
}

QString AppController::currentKoboSyncMessage() const
{
    return currentKoboSyncInstalled() ? m_koboSyncStatus.value(m_currentKobo).message : QString();
}

QString AppController::currentKoboSyncFix() const
{
    const KoboSyncStatus status = m_koboSyncStatus.value(m_currentKobo);
    if (!currentKoboSyncInstalled() || !status.needsAction()) {
        return {};
    }
    return status.reinstallFixes() ? QStringLiteral("reinstall") : QStringLiteral("changeKey");
}

bool AppController::koboSyncBusy() const
{
    return m_koboSyncRunner != nullptr;
}

QString AppController::koboSyncResult() const
{
    return m_koboSyncResult;
}

bool AppController::koboSyncResultOk() const
{
    return m_koboSyncResultOk;
}

void AppController::checkKoboSync(const QString &serial)
{
    auto *runner = new KoboSyncRunner(m_settings, m_apiKeyStore, m_network, this);
    connect(runner, &Runner::finished, this, [this, runner, serial](int) {
        runner->deleteLater();
        m_koboSyncStatus.insert(serial, runner->status());
        reloadKobos();
        rebuildBooks();
        Q_EMIT koboSyncChanged();
    });
    KoboSyncRunner::Options options;
    options.action = KoboSyncRunner::Action::Status;
    options.kobo = serial;
    runner->run(options);
}

void AppController::runKoboSync(const QString &serial, const QString &action)
{
    static const QHash<QString, KoboSyncRunner::Action> actions{
        {QStringLiteral("install"), KoboSyncRunner::Action::Install},
        {QStringLiteral("uninstall"), KoboSyncRunner::Action::Uninstall},
        {QStringLiteral("reinstall"), KoboSyncRunner::Action::Reinstall},
        {QStringLiteral("changeKey"), KoboSyncRunner::Action::SetKey},
        {QStringLiteral("reset"), KoboSyncRunner::Action::Reset},
    };
    if (m_koboSyncRunner || !actions.contains(action)) {
        return;
    }
    m_koboSyncRunner = new KoboSyncRunner(m_settings, m_apiKeyStore, m_network, this);
    auto lines = std::make_shared<QStringList>();
    auto errors = std::make_shared<QStringList>();
    connect(m_koboSyncRunner, &Runner::output, this, [lines, errors](Runner::OutputKind kind, const QString &text) {
        (kind == Runner::OutputKind::Line ? lines : errors)->append(text);
    });
    connect(m_koboSyncRunner, &KoboSyncRunner::credentialsNeeded, this, &AppController::komgaCredentialsNeeded);
    connect(m_koboSyncRunner, &Runner::finished, this, [this, serial, lines, errors](int exitCode) {
        m_koboSyncRunner->deleteLater();
        m_koboSyncRunner = nullptr;
        m_koboSyncResultOk = exitCode == 0;
        m_koboSyncResult = (m_koboSyncResultOk ? *lines : *errors + *lines).join(QLatin1Char('\n'));
        m_koboSyncStatus.remove(serial);
        // Check again, now that the endpoint or the key changed.
        m_koboSyncChecked.clear();
        reloadKobos();
        rebuildBooks();
        Q_EMIT koboSyncChanged();
    });

    KoboSyncRunner::Options options;
    options.action = actions.value(action);
    options.kobo = serial;
    options.createKey = true;
    m_koboSyncResult.clear();
    Q_EMIT koboSyncChanged();
    m_koboSyncRunner->run(options);
}

void AppController::provideKomgaCredentials(const QString &email, const QString &password)
{
    if (m_koboSyncRunner) {
        m_koboSyncRunner->provideCredentials(email, password);
    }
}

void AppController::cancelKomgaCredentials()
{
    if (m_koboSyncRunner) {
        m_koboSyncRunner->cancelCredentials();
    }
}

// --- Mounting and ejecting -------------------------------------------------------------------

void AppController::setVolumeBackend(VolumeBackend *backend)
{
    delete m_mounter;
    m_mounter = new KoboMounter(m_settings, backend ? backend : m_solidVolumes.get(), this);
    reloadKobos();
}

bool AppController::currentKoboMountable() const
{
    for (const UnmountedKobo &item : m_unmounted) {
        if (item.kobo && item.kobo->serial == m_currentKobo) {
            return true;
        }
    }
    return false;
}

int AppController::unknownUnmountedCount() const
{
    return int(std::count_if(m_unmounted.cbegin(), m_unmounted.cend(), [](const UnmountedKobo &item) {
        return !item.kobo;
    }));
}

void AppController::setSeriesOnConnect(const QString &serial, const QString &name, const QString &mountPoint)
{
    auto *watcher = new QFutureWatcher<KoboBooks::ApplyResult>(this);
    connect(watcher, &QFutureWatcher<KoboBooks::ApplyResult>::finished, this, [this, watcher, name]() {
        watcher->deleteLater();
        const KoboBooks::ApplyResult result = watcher->result();
        if (!result.ok) {
            Q_EMIT seriesSetOnConnect(false, i18n("The series could not be set on %1: %2", name, result.errorString));
        } else if (result.changed > 0) {
            Q_EMIT seriesSetOnConnect(true,
                                      i18np("Series set for 1 book on %2. Eject the Kobo to see it grouped.",
                                            "Series set for %1 books on %2. Eject the Kobo to see them grouped.",
                                            result.changed,
                                            name));
        }
    });
    watcher->setFuture(QtConcurrent::run([serial, mountPoint]() {
        KoboBooks books(mountPoint, serial);
        return books.setSeries(KoboBooks::manifestSeries(mountPoint));
    }));
}

void AppController::setShowSeriesSteps(bool show)
{
    m_settings->setShowSeriesSteps(show);
    m_settings->sync();
    Q_EMIT currentKoboChanged();
}

bool AppController::seriesOnConnect() const
{
    return m_settings->seriesOnConnect();
}

void AppController::setSeriesOnConnect(bool on)
{
    m_settings->setSeriesOnConnect(on);
    m_settings->sync();
    Q_EMIT currentKoboChanged();
}

bool AppController::ejectAfterSync() const
{
    return m_settings->ejectAfterSync();
}

void AppController::setEjectAfterSync(bool eject)
{
    m_settings->setEjectAfterSync(eject);
    m_settings->sync();
    Q_EMIT currentKoboChanged();
}

void AppController::mountKobo(const QString &serial)
{
    QString udi;
    for (const UnmountedKobo &item : std::as_const(m_unmounted)) {
        if ((serial.isEmpty() && !item.kobo) || (item.kobo && item.kobo->serial == serial)) {
            udi = item.udi;
            break;
        }
    }
    if (udi.isEmpty()) {
        Q_EMIT mountResult(false, i18n("This Kobo is not waiting to be mounted."));
        return;
    }
    m_mounter->mount(udi, [this](bool ok, const QString &message) {
        reloadKobos();
        Q_EMIT mountResult(ok, ok ? i18n("Mounted at %1.", message) : message);
    });
}

void AppController::ejectCurrentKobo()
{
    ejectMountPoint(m_currentKoboMount);
}

void AppController::ejectKobo(const QString &serial)
{
    for (const QVariantMap &row : m_kobos->items()) {
        if (row.value(QStringLiteral("serial")).toString() == serial) {
            ejectMountPoint(row.value(QStringLiteral("mountPoint")).toString());
            return;
        }
    }
}

void AppController::ejectMountPoint(const QString &mountPoint)
{
    if (m_busy || m_koboSyncRunner || m_koboBooks->busy() || mountPoint.isEmpty()) {
        Q_EMIT mountResult(false, i18n("The Kobo is in use; wait for the operation to finish."));
        return;
    }
    m_mounter->eject(mountPoint, [this](bool ok, const QString &message) {
        if (ok) {
            m_lastSyncWrote = false;
            Q_EMIT busyChanged();
        }
        reloadKobos();
        Q_EMIT mountResult(ok, message);
    });
}

// --- Shelves --------------------------------------------------------------------------------

void AppController::setShelfDatabase(std::unique_ptr<ShelfDatabase> database)
{
    m_client->setBookSource(nullptr);
    m_shelfLibrary.reset();
    m_shelfDatabase = std::move(database);
    m_shelfLibrary = std::make_unique<ShelfLibrary>(m_shelfDatabase.get());
    m_client->setBookSource(m_shelfLibrary.get());
    rebuildShelves();
    // Shelves not scanned for a day are scanned at start (SPEC-SHELF.md §3).
    for (const Shelf &shelf : m_shelfDatabase->shelves()) {
        if (!shelf.lastScan.isValid() || shelf.lastScan.secsTo(QDateTime::currentDateTimeUtc()) > 24 * 3600) {
            m_shelvesToScan.append(shelf.id);
        }
    }
    QTimer::singleShot(0, this, &AppController::scanNextShelf);
}

VariantListModel *AppController::shelves() const
{
    return m_shelves;
}

bool AppController::hasShelves() const
{
    return m_shelves->rowCount() > 0;
}

bool AppController::shelfScanning() const
{
    return m_shelfScanner != nullptr;
}

QString AppController::shelfScanStatus() const
{
    return m_shelfScanStatus;
}

void AppController::rebuildShelves()
{
    QList<QVariantMap> rows;
    if (m_shelfDatabase) {
        for (const Shelf &shelf : m_shelfDatabase->shelves()) {
            rows.append({{QStringLiteral("id"), shelf.id},
                         {QStringLiteral("name"), shelf.name},
                         {QStringLiteral("path"), shelf.path},
                         {QStringLiteral("books"), int(m_shelfDatabase->books(shelf.id).size())},
                         {QStringLiteral("lastScan"), shelf.lastScan.isValid() ? QLocale().toString(shelf.lastScan.toLocalTime(), QLocale::ShortFormat) : QString()},
                         {QStringLiteral("available"), QFileInfo(shelf.path).isDir()}});
        }
    }
    m_shelves->setItems(rows);
    Q_EMIT shelvesChanged();
}

void AppController::mergeShelves()
{
    m_libraryList = m_komgaLibraries;
    m_allSeries = m_komgaSeries;
    if (m_shelfDatabase) {
        for (const Shelf &shelf : m_shelfDatabase->shelves()) {
            KomgaLibrary library;
            library.id = ShelfLibrary::Prefix + QString::number(shelf.id);
            library.name = shelf.name;
            m_libraryList.append(library);
            for (const ShelfSeries &series : m_shelfLibrary->series(shelf.id)) {
                m_allSeries.append(m_shelfLibrary->toKomgaSeries(series));
            }
        }
    }
    QList<QVariantMap> rows;
    for (const KomgaLibrary &library : std::as_const(m_libraryList)) {
        rows.append({{QStringLiteral("id"), library.id}, {QStringLiteral("name"), library.name}});
    }
    m_libraries->setItems(rows);
    updateAdultSeries();
    Q_EMIT libraryChanged();
    refreshViews();
}

QString AppController::addShelf(const QString &name, const QUrl &folder)
{
    if (!m_shelfDatabase) {
        return i18n("The shelf database cannot be opened.");
    }
    QString error;
    const QString path = folder.toLocalFile();
    const QString shelfName = name.trimmed().isEmpty() ? QFileInfo(path).fileName() : name;
    const auto shelf = m_shelfDatabase->addShelf(shelfName, path, &error);
    if (!shelf) {
        return error;
    }
    rebuildShelves();
    scanShelf(shelf->id);
    return {};
}

void AppController::shelvesEdited(const QStringList &bookIds, const QStringList &seriesIds)
{
    for (const QString &id : bookIds) {
        m_thumbnails->forget(ThumbnailCache::Kind::Book, id);
    }
    for (const QString &id : seriesIds) {
        m_thumbnails->forget(ThumbnailCache::Kind::Series, id);
    }
    ++m_coverStamp;
    Q_EMIT coverStampChanged();
    rebuildShelves();
    mergeShelves();
    // What is shown again: the same series, or the one the book moved to.
    if (ShelfLibrary::isShelfId(m_currentId) && m_currentKind == QLatin1String("series")) {
        QString id = m_currentId;
        if (m_shelfLibrary && !m_shelfLibrary->findSeries(id) && !bookIds.isEmpty()) {
            if (const auto found = m_shelfLibrary->findBook(bookIds.first())) {
                id = found->first.id;
            }
        }
        openItem(m_currentKind, id);
    }
}

QString AppController::createKoboldLibrary()
{
    if (!m_shelfDatabase) {
        return i18n("The shelf database cannot be opened.");
    }
    QString error;
    const auto shelf = ShelfFolder::createLibrary(m_shelfDatabase.get(), koboldLibraryParent(), &error);
    if (!shelf) {
        return error;
    }
    rebuildShelves();
    scanShelf(shelf->id);
    return {};
}

QStringList AppController::seriesBookIds(const QString &seriesId) const
{
    QStringList ids;
    if (const auto series = m_shelfLibrary ? m_shelfLibrary->findSeries(seriesId) : std::nullopt) {
        for (const ShelfBook &book : series->books) {
            ids.append(ShelfLibrary::bookId(book.shelfId, book.id));
        }
    }
    return ids;
}

QString AppController::koboldLibraryParent() const
{
    return m_koboldLibraryParent.isEmpty() ? ShelfFolder::defaultParent() : m_koboldLibraryParent;
}

QString AppController::renameShelf(qint64 shelfId, const QString &name)
{
    QString error;
    if (!m_shelfDatabase || !m_shelfDatabase->renameShelf(shelfId, name, &error)) {
        return error;
    }
    rebuildShelves();
    mergeShelves();
    return {};
}

void AppController::removeShelf(qint64 shelfId)
{
    if (!m_shelfDatabase || m_shelfScanner) {
        return;
    }
    m_shelfDatabase->removeShelf(shelfId);
    rebuildShelves();
    mergeShelves();
}

void AppController::scanShelf(qint64 shelfId)
{
    if (!m_shelfDatabase) {
        return;
    }
    for (const Shelf &shelf : m_shelfDatabase->shelves()) {
        if ((shelfId < 0 || shelf.id == shelfId) && !m_shelvesToScan.contains(shelf.id)) {
            m_shelvesToScan.append(shelf.id);
        }
    }
    scanNextShelf();
}

void AppController::scanNextShelf()
{
    if (m_shelfScanner || m_shelvesToScan.isEmpty() || !m_shelfDatabase) {
        return;
    }
    const qint64 shelfId = m_shelvesToScan.takeFirst();
    const auto shelf = m_shelfDatabase->shelf(shelfId);
    if (!shelf) {
        scanNextShelf();
        return;
    }
    m_shelfScanner = new ShelfScanner(m_shelfDatabase.get(), this);
    m_shelfScanStatus = i18n("Scanning %1…", shelf->name);
    Q_EMIT shelvesChanged();
    connect(m_shelfScanner, &ShelfScanner::progress, this, [this, name = shelf->name](int done, int total) {
        m_shelfScanStatus = i18n("Scanning %1: %2 of %3", name, done, total);
        Q_EMIT shelvesChanged();
    });
    connect(m_shelfScanner, &ShelfScanner::finished, this, [this, name = shelf->name](const ShelfScanReport &report) {
        m_shelfScanner->deleteLater();
        m_shelfScanner = nullptr;
        m_shelfScanStatus = report.ok ? i18n("%1: %2 new, %3 changed, %4 missing.", name, report.added + report.moved, report.updated, report.missing)
                                      : report.errorString;
        rebuildShelves();
        mergeShelves();
        reloadSelection();
        scanNextShelf();
    });
    m_shelfScanner->scan(shelfId);
}

// --- Removal confirmations -------------------------------------------------------------

QVariantList AppController::removalQuestions() const
{
    const HiddenContent hidden = hiddenContent();
    QVariantList questions;
    for (const RemovalQuestion &question : m_removalQuestions) {
        questions.append(QVariantMap{
            {QStringLiteral("bookId"), question.bookId},
            {QStringLiteral("kind"), int(question.kind)},
            {QStringLiteral("label"), hidden.book(question.bookId, question.seriesId, question.label)},
            {QStringLiteral("size"), QLocale().formattedDataSize(question.sizeBytes)},
            {QStringLiteral("removeByDefault"), question.removeByDefault()},
        });
    }
    return questions;
}

VariantListModel *AppController::keptBooks() const
{
    return m_keptBooks;
}

void AppController::answerRemovals(const QVariantMap &answers)
{
    if (!m_activeSync) {
        return;
    }
    QHash<QString, bool> remove;
    for (auto it = answers.cbegin(); it != answers.cend(); ++it) {
        remove.insert(it.key(), it.value().toBool());
    }
    m_activeSync->answerRemovals(remove);
}

void AppController::cancelRemovals()
{
    if (m_activeSync) {
        m_activeSync->cancelRemovals();
    }
}

QString AppController::removeKeptBook(const QString &bookId)
{
    if (m_busy || m_currentKoboMount.isEmpty()) {
        return i18n("Connect the Kobo first.");
    }
    QString error;
    if (!removeManagedBook(m_currentKoboMount, bookId, &error)) {
        return error;
    }
    rebuildKeptBooks();
    return {};
}

void AppController::rebuildKeptBooks()
{
    QList<QVariantMap> rows;
    if (!m_currentKoboMount.isEmpty()) {
        Manifest manifest(m_currentKoboMount);
        if (manifest.load()) {
            for (const ManifestEntry &entry : manifest.entries()) {
                if (!entry.kept) {
                    continue;
                }
                rows.append({{QStringLiteral("bookId"), entry.komgaBookId},
                             {QStringLiteral("label"), QFileInfo(entry.path).completeBaseName().remove(QStringLiteral(".kepub"))},
                             {QStringLiteral("reason"),
                              entry.keptReason == QLatin1String("not in Komga")       ? i18n("No longer in Komga")
                                  : entry.keptReason == QLatin1String("not on its shelf") ? i18n("No longer on its shelf")
                                                                                          : i18n("Left the selection")}});
            }
        }
    }
    m_keptBooks->setItems(rows);
}

// --- Parental control ---------------------------------------------------------------------

void AppController::setParentalPinStore(ApiKeyStore *pinStore)
{
    delete m_parental;
    m_parental = new ParentalLock(m_settings, pinStore, this);
    connect(m_parental, &ParentalLock::changed, this, &AppController::onParentalChanged);
    onParentalChanged();
}

bool AppController::hidesSeries(const QString &seriesId) const
{
    return !seriesId.isEmpty() && m_adultSeries.contains(seriesId);
}

void AppController::updateAdultSeries()
{
    m_adultSeries.clear();
    if (m_parental && m_parental->hidesAdult()) {
        for (const KomgaSeries &series : std::as_const(m_allSeries)) {
            if (m_parental->isAdult(series)) {
                m_adultSeries.insert(series.id);
            }
        }
    }
}

void AppController::loadReadListSeries()
{
    if (m_readListSeriesLoaded || !parentalEnabled() || m_komgaState != QLatin1String("ready")) {
        return;
    }
    m_readListSeriesLoaded = true;
    for (const KomgaReadList &readList : std::as_const(m_allReadLists)) {
        m_client->fetchReadListBooks(readList.id, QString(), [this, id = readList.id](const KomgaResult<QList<KomgaBook>> &books) {
            QSet<QString> series;
            for (const KomgaBook &book : books.value) {
                series.insert(book.seriesId);
            }
            m_readListSeries.insert(id, series);
            rebuildGroups();
            rebuildSidebar();
        });
    }
}

void AppController::onParentalChanged()
{
    updateAdultSeries();
    loadReadListSeries();
    // The open series may have just become hidden.
    if (m_currentKind == QLatin1String("series") && hidesSeries(m_currentId)) {
        m_currentKind.clear();
        m_currentId.clear();
        m_seriesBooks.clear();
    }
    Q_EMIT parentalChanged();
    refreshViews();
    Q_EMIT selectionChanged();
}

bool AppController::eventFilter(QObject *watched, QEvent *event)
{
    if (m_parental) {
        switch (event->type()) {
        case QEvent::KeyPress:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
            m_parental->touch();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

bool AppController::parentalEnabled() const
{
    return m_parental && m_parental->enabled();
}

bool AppController::parentalLocked() const
{
    return m_parental && m_parental->hidesAdult();
}

int AppController::parentalAgeLimit() const
{
    return m_settings->parentalRules().ageLimit;
}

QStringList AppController::parentalKeywords() const
{
    return m_settings->parentalRules().keywords;
}

int AppController::parentalAutoLockMinutes() const
{
    return m_settings->parentalAutoLockMinutes();
}

int AppController::hiddenSelectionCount() const
{
    return m_hiddenSelectionCount;
}

QVariantList AppController::komgaGenres() const
{
    ParentalRules rules = m_settings->parentalRules();
    QVariantList genres;
    for (const QString &genre : m_komgaGenres) {
        genres.append(QVariantMap{{QStringLiteral("name"), genre}, {QStringLiteral("adult"), rules.genreMatches(genre)}});
    }
    return genres;
}

void AppController::unlockParental(const QString &pin)
{
    m_parental->unlock(pin, [this](bool ok, const QString &message) {
        Q_EMIT parentalResult(ok, message);
    });
}

void AppController::enableParental(const QString &pin)
{
    m_parental->enable(pin, [this](bool ok, const QString &message) {
        Q_EMIT parentalResult(ok, message);
    });
}

void AppController::disableParental(const QString &pin)
{
    m_parental->disable(pin, [this](bool ok, const QString &message) {
        Q_EMIT parentalResult(ok, message);
    });
}

void AppController::lockParental()
{
    m_parental->lock();
}

QString AppController::setParentalRules(int ageLimit, const QStringList &keywords)
{
    QString error;
    m_parental->setRules(ageLimit, keywords, &error);
    return error;
}

void AppController::setParentalAutoLockMinutes(int minutes)
{
    m_parental->setAutoLockMinutes(minutes);
    Q_EMIT parentalChanged();
}

void AppController::loadKomgaGenres()
{
    if (m_komgaState != QLatin1String("ready")) {
        return;
    }
    m_client->fetchGenres([this](const KomgaResult<QStringList> &result) {
        if (result.ok()) {
            m_komgaGenres = result.value;
            Q_EMIT parentalChanged();
        }
    });
}

HiddenContent AppController::hiddenContent() const
{
    HiddenContent hidden;
    if (!parentalLocked()) {
        return hidden;
    }
    hidden.seriesIds = m_adultSeries;
    for (const KomgaBook &book : std::as_const(m_directBooks)) {
        if (hidesSeries(book.seriesId)) {
            hidden.bookIds.insert(book.id);
        }
    }
    for (const ResolvedGroup &group : std::as_const(m_resolved.groups)) {
        for (const KomgaBook &book : group.books) {
            if (hidesSeries(book.seriesId)) {
                hidden.bookIds.insert(book.id);
            }
        }
    }
    return hidden;
}

// --- Application icon ---------------------------------------------------------------------

namespace
{
const QStringList AppIcons{QStringLiteral("peek"), QStringLiteral("sync"), QStringLiteral("face")};
const QString DefaultAppIcon = QStringLiteral("peek");

QString appIconResource(const QString &icon)
{
    return QStringLiteral(":/qt/qml/me/kodara/kobold/icons/") + icon + QStringLiteral(".svg");
}

// The user's copy, which takes precedence over the installed icon in the launcher.
QString launcherIconPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/icons/hicolor/scalable/apps/me.kodara.kobold.svg");
}
}

QString AppController::appIcon() const
{
    const QString icon = m_settings->appIcon();
    return AppIcons.contains(icon) ? icon : DefaultAppIcon;
}

void AppController::setAppIcon(const QString &icon)
{
    if (!AppIcons.contains(icon) || icon == appIcon()) {
        return;
    }
    m_settings->setAppIcon(icon);
    m_settings->sync();
    applyAppIcon(true);
    Q_EMIT appIconChanged();
}

QStringList AppController::appIcons() const
{
    return AppIcons;
}

QString AppController::appIconUrl(const QString &icon) const
{
    return QStringLiteral("qrc") + appIconResource(AppIcons.contains(icon) ? icon : DefaultAppIcon);
}

void AppController::applyAppIcon(bool updateLauncher)
{
    const QString resource = appIconResource(appIcon());
    if (qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        QGuiApplication::setWindowIcon(QIcon(resource));
    }
    KAboutData about = KAboutData::applicationData();
    about.setProgramLogo(QUrl(QStringLiteral("qrc") + resource));
    KAboutData::setApplicationData(about);

    if (!updateLauncher) {
        return;
    }
    const QString path = launcherIconPath();
    QFile::remove(path);
    if (appIcon() != DefaultAppIcon) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        if (QFile::copy(resource, path)) {
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        }
    }
}

void AppController::sync(bool dryRun)
{
    if (m_busy || m_currentKoboMount.isEmpty()) {
        return;
    }
    runSyncChain({m_currentKoboMount}, dryRun);
}

void AppController::syncAll(bool dryRun)
{
    if (m_busy) {
        return;
    }
    QStringList mountPoints;
    for (const ConnectedKobo &device : connectedKobos(m_settings)) {
        mountPoints.append(device.mountPoint);
    }
    if (mountPoints.isEmpty()) {
        m_log->clear();
        m_log->append({{QStringLiteral("kind"), QStringLiteral("error")}, {QStringLiteral("message"), i18n("No Kobo connected")}});
        return;
    }
    runSyncChain(mountPoints, dryRun);
}

void AppController::runSyncChain(QStringList mountPoints, bool dryRun)
{
    m_busy = true;
    m_lastSyncWrote = false;
    m_failures.clear();
    m_log->clear();
    m_lastExitCode = 0;
    m_operationTitle = dryRun ? i18nc("@title:dialog", "Preview") : i18nc("@title:dialog", "Sync");
    m_lastDryRun = dryRun;
    m_lastSeriesPending = 0;
    resetProgress();
    Q_EMIT busyChanged();

    auto codes = std::make_shared<QList<int>>();
    // Kobos the sync changed, ejected at the end when asked (SPEC-MOUNT.md §4).
    auto toEject = std::make_shared<QStringList>();
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, mountPoints, dryRun, codes, toEject, next]() mutable {
        if (codes->size() == mountPoints.size()) {
            // Same rule as the CLI (SPEC-DEVICES.md §5).
            int combined = 0;
            for (int code : std::as_const(*codes)) {
                if (code >= 2) {
                    combined = code;
                    break;
                }
                if (code == 1) {
                    combined = 1;
                }
            }
            m_lastExitCode = combined;
            m_lastSyncWrote = !dryRun;
            m_busy = false;
            Q_EMIT busyChanged();
            reloadKobos();
            rebuildKeptBooks();
            for (const QString &mountPoint : std::as_const(*toEject)) {
                m_mounter->eject(mountPoint, [this](bool ok, const QString &message) {
                    if (ok) {
                        m_lastSyncWrote = false;
                        Q_EMIT busyChanged();
                    }
                    reloadKobos();
                    Q_EMIT mountResult(ok, message);
                });
            }
            *next = nullptr;
            return;
        }
        const QString mountPoint = mountPoints.at(codes->size());
        if (mountPoints.size() > 1) {
            m_log->append({{QStringLiteral("kind"), QStringLiteral("header")}, {QStringLiteral("message"), mountPoint}});
        }
        auto *runner = new SyncRunner(m_settings, m_apiKeyStore, m_network, this);
        m_activeSync = runner;
        connect(runner, &SyncRunner::stepped, this, [this](int done, int total) {
            m_progressDone = done;
            m_progressTotal = total;
            Q_EMIT progressChanged();
        });
        connect(runner, &SyncRunner::removalsToConfirm, this, [this](const QList<RemovalQuestion> &questions) {
            m_removalQuestions = questions;
            Q_EMIT removalsToConfirm();
        });
        connect(runner, &Runner::finished, this, [this, runner, codes, toEject, dryRun, next](int code) {
            codes->append(code);
            if (!dryRun) {
                m_lastSeriesPending += runner->seriesPending();
            }
            if (!dryRun && code <= 1 && runner->changedKobo() && m_settings->ejectAfterSync()) {
                toEject->append(runner->deviceRoot());
            }
            m_activeSync = nullptr;
            m_removalQuestions.clear();
            runner->deleteLater();
            (*next)();
        });
        runOperation(runner, [dryRun, mountPoint, hidden = hiddenContent()](Runner *r) {
            SyncRunner::Options options;
            options.dryRun = dryRun;
            options.devicePath = mountPoint;
            options.hidden = hidden;
            options.askRemovals = true;
            static_cast<SyncRunner *>(r)->run(options);
        });
    };
    (*next)();
}

void AppController::pullProgress(bool dryRun)
{
    if (m_busy || m_currentKoboMount.isEmpty()) {
        return;
    }
    m_busy = true;
    m_lastSyncWrote = false;
    m_failures.clear();
    m_log->clear();
    m_operationTitle = dryRun ? i18nc("@title:dialog", "Preview Reading Progress") : i18nc("@title:dialog", "Send Reading Progress");
    m_lastDryRun = dryRun;
    m_lastSeriesPending = 0;
    resetProgress();
    Q_EMIT busyChanged();

    auto *runner = new ProgressRunner(m_settings, m_apiKeyStore, m_network, this);
    connect(runner, &Runner::finished, this, [this, runner](int code) {
        m_lastExitCode = code;
        m_busy = false;
        Q_EMIT busyChanged();
        runner->deleteLater();
    });
    const QString mountPoint = m_currentKoboMount;
    runOperation(runner, [dryRun, mountPoint, hidden = hiddenContent()](Runner *r) {
        static_cast<ProgressRunner *>(r)->run(ProgressRunner::Options{dryRun, mountPoint, QString(), hidden, false});
    });
}

// --- Diagnostics ----------------------------------------------------------------------

VariantListModel *AppController::diagnostics() const
{
    return m_diagnostics;
}

bool AppController::diagnosticsRunning() const
{
    return m_diagnosticsRunning;
}

void AppController::runDiagnostics()
{
    if (m_diagnosticsRunning) {
        return;
    }
    m_diagnosticsRunning = true;
    m_diagnostics->clear();
    Q_EMIT diagnosticsChanged();

    auto *runner = new DiagnosticsRunner(DiagnosticsRunner::Mode::Doctor, m_settings, m_apiKeyStore, m_network, this);
    connect(runner, &DiagnosticsRunner::check, this, [this](int level, const QString &message, const QString &hint) {
        m_diagnostics->append({{QStringLiteral("level"), level}, {QStringLiteral("message"), message}, {QStringLiteral("hint"), hint}});
    });
    connect(runner, &Runner::finished, this, [this, runner]() {
        m_diagnosticsRunning = false;
        Q_EMIT diagnosticsChanged();
        runner->deleteLater();
    });
    runner->run(m_currentKoboMount, QString());
}

// --- Settings ------------------------------------------------------------------------

QString AppController::setKomgaUrl(const QString &url)
{
    QString errorString;
    if (!m_settings->setKomgaUrl(url, &errorString)) {
        return errorString;
    }
    if (!m_settings->sync()) {
        return i18n("Cannot write the configuration file");
    }
    Q_EMIT komgaChanged();
    start();
    return {};
}

void AppController::checkAndSaveApiKey(const QString &apiKey)
{
    const QString key = apiKey.trimmed();
    if (key.isEmpty()) {
        Q_EMIT apiKeyResult(false, i18n("Enter an API key."));
        return;
    }
    auto *client = new KomgaClient(m_network, this);
    client->setBaseUrl(m_settings->komgaUrl());
    client->setApiKey(key);
    client->fetchCurrentUser([this, client, key](const KomgaResult<KomgaUser> &user) {
        client->deleteLater();
        if (user.error.kind == KomgaError::Kind::Unauthorized) {
            Q_EMIT apiKeyResult(false, i18n("Komga refused this API key; it was not stored."));
            return;
        }
        const bool verified = user.ok();
        m_apiKeyStore->write(key, [this, key, verified, user](const ApiKeyStore::Result &result) {
            if (result.status != ApiKeyStore::Result::Status::Ok) {
                Q_EMIT apiKeyResult(false, i18n("Cannot store the API key: %1", result.errorString));
                return;
            }
            Q_EMIT apiKeyResult(true,
                                verified ? i18n("API key stored. Connected as %1.", user.value.email)
                                         : i18n("API key stored, but Komga could not be reached to check it (%1).", user.error.message));
            connectWithKey(key);
        });
    });
}

void AppController::setBackupBeforeSync(const QString &serial, bool backup)
{
    m_settings->setBackupBeforeSync(serial, backup);
    m_settings->sync();
    reloadKobos();
}

KoboBooksAdultFilter AppController::koboBooksAdultFilter() const
{
    KoboBooksAdultFilter filter;
    if (!m_parental || !m_parental->hidesAdult()) {
        return filter;
    }
    filter.active = true;
    // Without the library, adult series are unknown: Komga and Kobold books stay out.
    filter.unknown = m_allSeries.isEmpty();
    for (const KomgaSeries &series : std::as_const(m_allSeries)) {
        if (m_adultSeries.contains(series.id)) {
            filter.titles.insert(foldForComparison(series.title));
        }
    }
    return filter;
}

QString AppController::renameKobo(const QString &serial, const QString &name)
{
    QString errorString;
    if (!m_settings->renameKobo(serial, name, &errorString)) {
        return errorString;
    }
    m_settings->sync();
    reloadKobos();
    return {};
}

void AppController::setComicProfile(const QString &serial, const QString &profile)
{
    if (profile.isEmpty()) {
        m_settings->clearComicProfileOverride(serial);
    } else {
        m_settings->setComicProfileOverride(serial, profile);
    }
    m_settings->sync();
    reloadKobos();
}

qint64 AppController::coverCacheSize() const
{
    return m_thumbnails->sizeBytes();
}

void AppController::clearCoverCache()
{
    m_thumbnails->clear();
    Q_EMIT coverCacheChanged();
}

QStringList AppController::comicProfiles() const
{
    return kccKoboProfiles();
}

} // namespace Kobold
