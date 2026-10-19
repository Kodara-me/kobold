// SPDX-License-Identifier: MIT

#pragma once

#include "kobobookscontroller.h"
#include "kobomounter.h"
#include "kobosyncrunner.h"
#include "parentallock.h"
#include "shelfeditcontroller.h"
#include "shelfdatabase.h"
#include "shelflibrary.h"
#include "shelfscanner.h"
#include "syncexecutor.h"
#include "komgaclient.h"
#include "selectionresolver.h"
#include "variantlistmodel.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <qqmlregistration.h>

#include <memory>

class QNetworkAccessManager;
class QTimer;
class QQmlEngine;
class QJSEngine;

namespace Kobold
{

class ApiKeyStore;
class Runner;
class SelectionStore;
class SyncRunner;
class Settings;
class ThumbnailCache;

// State and actions of the application, exposed to QML (SPEC-APP.md §3). Every rule comes
// from the core and services libraries; this class only adapts them to list models.
class AppController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "connecting", "noKey", "error" or "ready".
    Q_PROPERTY(QString komgaState READ komgaState NOTIFY komgaChanged)
    Q_PROPERTY(QString komgaMessage READ komgaMessage NOTIFY komgaChanged)
    Q_PROPERTY(QString komgaUrl READ komgaUrl NOTIFY komgaChanged)
    Q_PROPERTY(QString userEmail READ userEmail NOTIFY komgaChanged)

    Q_PROPERTY(Kobold::VariantListModel *kobos READ kobos CONSTANT)
    // The Manage Books window of one Kobo (SPEC-KOBO-BOOKS.md §6).
    Q_PROPERTY(Kobold::KoboBooksController *koboBooks READ koboBooks CONSTANT)
    // The metadata editors of shelf books and series (SPEC-SHELF.md §4.3, §4.4).
    Q_PROPERTY(Kobold::ShelfEditController *shelfEditor READ shelfEditor CONSTANT)
    // Appended to cover addresses: changes when a shelf cover was edited.
    Q_PROPERTY(QString coverStamp READ coverStamp NOTIFY coverStampChanged)
    Q_PROPERTY(QString currentKobo READ currentKobo WRITE setCurrentKobo NOTIFY currentKoboChanged)
    Q_PROPERTY(QString currentKoboName READ currentKoboName NOTIFY currentKoboChanged)
    Q_PROPERTY(bool currentKoboConnected READ currentKoboConnected NOTIFY currentKoboChanged)
    // Komga Kobo sync of the current Kobo (SPEC-KOBO-SYNC.md §8): the problem found when it was
    // connected, and its fix ("reinstall" or "changeKey").
    Q_PROPERTY(bool currentKoboSyncInstalled READ currentKoboSyncInstalled NOTIFY koboSyncChanged)
    Q_PROPERTY(QString currentKoboSyncMessage READ currentKoboSyncMessage NOTIFY koboSyncChanged)
    Q_PROPERTY(QString currentKoboSyncFix READ currentKoboSyncFix NOTIFY koboSyncChanged)
    Q_PROPERTY(bool koboSyncBusy READ koboSyncBusy NOTIFY koboSyncChanged)
    // Outcome of the last Komga sync action, for the settings page.
    Q_PROPERTY(QString koboSyncResult READ koboSyncResult NOTIFY koboSyncChanged)
    Q_PROPERTY(bool koboSyncResultOk READ koboSyncResultOk NOTIFY koboSyncChanged)

    Q_PROPERTY(Kobold::VariantListModel *libraries READ libraries CONSTANT)
    Q_PROPERTY(QString libraryFilter READ libraryFilter WRITE setLibraryFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString seriesSearch READ seriesSearch WRITE setSeriesSearch NOTIFY filtersChanged)
    Q_PROPERTY(bool multipleLibraries READ multipleLibraries NOTIFY libraryChanged)
    Q_PROPERTY(bool libraryLoading READ libraryLoading NOTIFY libraryChanged)
    Q_PROPERTY(Kobold::VariantListModel *series READ series CONSTANT)
    Q_PROPERTY(Kobold::VariantListModel *collections READ collections CONSTANT)
    Q_PROPERTY(Kobold::VariantListModel *readLists READ readLists CONSTANT)

    // Sidebar rows: section headers ("header") and their series, collections or read lists.
    Q_PROPERTY(Kobold::VariantListModel *sidebar READ sidebar CONSTANT)

    Q_PROPERTY(Kobold::VariantListModel *books READ books CONSTANT)
    // What the center pane shows: kind ("series", "collection", "readlist" or empty), id,
    // title, details, followed, volumes.
    Q_PROPERTY(QVariantMap currentItem READ currentItem NOTIFY currentItemChanged)
    Q_PROPERTY(bool booksLoading READ booksLoading NOTIFY currentItemChanged)

    Q_PROPERTY(Kobold::VariantListModel *selectionGroups READ selectionGroups CONSTANT)
    Q_PROPERTY(Kobold::VariantListModel *selectionBooks READ selectionBooks CONSTANT)
    // Selected files on the computer (SPEC-LOCAL.md §6), each {path, name, missing}.
    Q_PROPERTY(Kobold::VariantListModel *localFiles READ localFiles CONSTANT)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionError READ selectionError NOTIFY selectionChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // The running or last operation, for the progress dialog.
    Q_PROPERTY(QString operationTitle READ operationTitle NOTIFY busyChanged)
    Q_PROPERTY(QString currentStep READ currentStep NOTIFY progressChanged)
    // Actions done and their number; total 0 while unknown.
    Q_PROPERTY(int progressDone READ progressDone NOTIFY progressChanged)
    Q_PROPERTY(int progressTotal READ progressTotal NOTIFY progressChanged)
    Q_PROPERTY(Kobold::VariantListModel *log READ log CONSTANT)
    Q_PROPERTY(QStringList failures READ failures NOTIFY busyChanged)
    Q_PROPERTY(int lastExitCode READ lastExitCode NOTIFY busyChanged)
    // True when the last operation was a real sync that changed the Kobo (not a preview).
    Q_PROPERTY(bool lastSyncWrote READ lastSyncWrote NOTIFY busyChanged)

    Q_PROPERTY(Kobold::VariantListModel *diagnostics READ diagnostics CONSTANT)
    Q_PROPERTY(bool diagnosticsRunning READ diagnosticsRunning NOTIFY diagnosticsChanged)

    Q_PROPERTY(qint64 coverCacheSize READ coverCacheSize NOTIFY coverCacheChanged)
    Q_PROPERTY(QStringList comicProfiles READ comicProfiles CONSTANT)
    // Application icon (SPEC-APP.md §12.1): the id and the qrc URL of each choice.
    Q_PROPERTY(QString appIcon READ appIcon WRITE setAppIcon NOTIFY appIconChanged)
    Q_PROPERTY(QStringList appIcons READ appIcons CONSTANT)

    // Parental control (SPEC-PARENTAL.md): on/off, locked view, and how many selected items
    // the Kobo pane leaves out.
    Q_PROPERTY(bool parentalEnabled READ parentalEnabled NOTIFY parentalChanged)
    Q_PROPERTY(bool parentalLocked READ parentalLocked NOTIFY parentalChanged)
    Q_PROPERTY(int parentalAgeLimit READ parentalAgeLimit NOTIFY parentalChanged)
    Q_PROPERTY(QStringList parentalKeywords READ parentalKeywords NOTIFY parentalChanged)
    Q_PROPERTY(int parentalAutoLockMinutes READ parentalAutoLockMinutes NOTIFY parentalChanged)
    Q_PROPERTY(int hiddenSelectionCount READ hiddenSelectionCount NOTIFY selectionChanged)
    // Komga's genres, each {name, adult}, for the settings page.
    Q_PROPERTY(QVariantList komgaGenres READ komgaGenres NOTIFY parentalChanged)

    // Mounting and ejecting (SPEC-MOUNT.md): whether the current Kobo is plugged in but not
    // mounted, and the eject-after-sync setting.
    Q_PROPERTY(bool currentKoboMountable READ currentKoboMountable NOTIFY currentKoboChanged)
    Q_PROPERTY(int unknownUnmountedCount READ unknownUnmountedCount NOTIFY currentKoboChanged)
    Q_PROPERTY(bool ejectAfterSync READ ejectAfterSync WRITE setEjectAfterSync NOTIFY currentKoboChanged)
    Q_PROPERTY(bool seriesOnConnect READ seriesOnConnect WRITE setSeriesOnConnect NOTIFY currentKoboChanged)
    Q_PROPERTY(bool showSeriesSteps READ showSeriesSteps WRITE setShowSeriesSteps NOTIFY currentKoboChanged)
    // The last operation was a preview (its result is the list of changes).
    Q_PROPERTY(bool lastDryRun READ lastDryRun NOTIFY busyChanged)
    // Books the last sync copied with a series: they need the reconnection steps.
    Q_PROPERTY(int lastSeriesPending READ lastSeriesPending NOTIFY busyChanged)

    // Shelves (SPEC-SHELF.md): each {id, name, path, books, lastScan, available}.
    Q_PROPERTY(Kobold::VariantListModel *shelves READ shelves CONSTANT)
    Q_PROPERTY(bool hasShelves READ hasShelves NOTIFY shelvesChanged)
    Q_PROPERTY(bool shelfScanning READ shelfScanning NOTIFY shelvesChanged)
    Q_PROPERTY(QString shelfScanStatus READ shelfScanStatus NOTIFY shelvesChanged)

    // Removal confirmations (SPEC-UNSYNC.md): the questions of the running sync, each
    // {bookId, kind (1 or 3), label, size, removeByDefault}; and the books kept on the current
    // Kobo, each {bookId, label, reason}.
    Q_PROPERTY(QVariantList removalQuestions READ removalQuestions NOTIFY removalsToConfirm)
    Q_PROPERTY(Kobold::VariantListModel *keptBooks READ keptBooks CONSTANT)

public:
    AppController(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent = nullptr);
    ~AppController() override;

    // The instance QML uses; set by main() before loading QML.
    static void setInstance(AppController *instance);
    static AppController *create(QQmlEngine *qmlEngine, QJSEngine *jsEngine);

    ThumbnailCache *thumbnails() const;

    // Reads the API key from the keychain and connects; also lists the Kobos.
    Q_INVOKABLE void start();
    // Connects with this key without reading the keychain (tests, and after saving a key).
    void connectWithKey(const QString &apiKey);

    QString komgaState() const;
    QString komgaMessage() const;
    QString komgaUrl() const;
    QString userEmail() const;

    VariantListModel *kobos() const;
    QString currentKobo() const;
    void setCurrentKobo(const QString &serial);
    QString currentKoboName() const;
    bool currentKoboConnected() const;
    Q_INVOKABLE void reloadKobos();

    bool currentKoboSyncInstalled() const;
    QString currentKoboSyncMessage() const;
    QString currentKoboSyncFix() const;
    bool koboSyncBusy() const;
    QString koboSyncResult() const;
    bool koboSyncResultOk() const;
    // action: "install", "uninstall", "reinstall", "changeKey" or "reset".
    Q_INVOKABLE void runKoboSync(const QString &serial, const QString &action);
    // Answers komgaCredentialsNeeded(); the password is used once and not kept.
    Q_INVOKABLE void provideKomgaCredentials(const QString &email, const QString &password);
    Q_INVOKABLE void cancelKomgaCredentials();

    VariantListModel *libraries() const;
    QString libraryFilter() const;
    void setLibraryFilter(const QString &libraryId);
    QString seriesSearch() const;
    void setSeriesSearch(const QString &text);
    bool multipleLibraries() const;
    bool libraryLoading() const;
    VariantListModel *series() const;
    VariantListModel *collections() const;
    VariantListModel *readLists() const;
    Q_INVOKABLE void reloadLibrary();

    VariantListModel *sidebar() const;
    Q_INVOKABLE void toggleSection(const QString &sectionId);

    VariantListModel *books() const;
    QVariantMap currentItem() const;
    bool booksLoading() const;
    // kind: "series", "collection" or "readlist".
    Q_INVOKABLE void openItem(const QString &kind, const QString &id);
    Q_INVOKABLE void openSeries(const QString &seriesId);
    // Follows or stops following what the center pane shows; returns an error message.
    Q_INVOKABLE QString setCurrentItemFollowed(bool followed, const QString &volumes = QString());

    // Selection of the current Kobo. Each returns an error message, empty on success.
    Q_INVOKABLE QString setSeriesFollowed(const QString &seriesId, bool followed, const QString &volumes = QString());
    Q_INVOKABLE QString setBookSelected(const QString &bookId, bool selected);
    Q_INVOKABLE QString setCollectionFollowed(const QString &collectionId, bool followed);
    Q_INVOKABLE QString setReadListFollowed(const QString &readListId, bool followed);
    // kind: "series", "collection", "readlist" or "book".
    Q_INVOKABLE QString removeFromSelection(const QString &kind, const QString &id);
    // Error message for an invalid volume range, empty when valid (empty text is valid).
    Q_INVOKABLE QString checkVolumes(const QString &volumes) const;

    VariantListModel *selectionGroups() const;
    VariantListModel *selectionBooks() const;
    VariantListModel *localFiles() const;
    // Adds files chosen in a file dialog; returns an error message, empty on success.
    Q_INVOKABLE QString addLocalFiles(const QList<QUrl> &files);
    Q_INVOKABLE QString removeLocalFile(const QString &path);
    int selectionCount() const;
    QString selectionError() const;

    bool busy() const;
    QString operationTitle() const
    {
        return m_operationTitle;
    }
    QString currentStep() const
    {
        return m_currentStep;
    }
    int progressDone() const
    {
        return m_progressDone;
    }
    int progressTotal() const
    {
        return m_progressTotal;
    }
    VariantListModel *log() const;
    QStringList failures() const;
    int lastExitCode() const;
    bool lastSyncWrote() const;
    Q_INVOKABLE void sync(bool dryRun);
    Q_INVOKABLE void syncAll(bool dryRun);
    Q_INVOKABLE void pullProgress(bool dryRun);

    VariantListModel *diagnostics() const;
    bool diagnosticsRunning() const;
    Q_INVOKABLE void runDiagnostics();

    // Settings. Each returns an error message, empty on success.
    Q_INVOKABLE QString setKomgaUrl(const QString &url);
    // Checks the key against Komga, stores it unless refused; result in apiKeyResult().
    Q_INVOKABLE void checkAndSaveApiKey(const QString &apiKey);
    Q_INVOKABLE QString renameKobo(const QString &serial, const QString &name);
    // Back up the Kobo's database before each sync (SPEC-KOBO-BOOKS.md §4).
    Q_INVOKABLE void setBackupBeforeSync(const QString &serial, bool backup);
    KoboBooksController *koboBooks() const
    {
        return m_koboBooks;
    }
    ShelfEditController *shelfEditor() const
    {
        return m_shelfEditor;
    }
    QString coverStamp() const
    {
        return m_coverStamp == 0 ? QString() : QStringLiteral("?v=%1").arg(m_coverStamp);
    }
    // Adult series titles while parental control hides them, for the books of a Kobo.
    KoboBooksAdultFilter koboBooksAdultFilter() const;
    // An empty profile restores the automatic choice from the model.
    Q_INVOKABLE void setComicProfile(const QString &serial, const QString &profile);

    qint64 coverCacheSize() const;
    Q_INVOKABLE void clearCoverCache();

    QString appIcon() const;
    void setAppIcon(const QString &icon);
    QStringList appIcons() const;
    // qrc URL of an icon, for QML images.
    Q_INVOKABLE QString appIconUrl(const QString &icon) const;

    bool parentalEnabled() const;
    bool parentalLocked() const;
    int parentalAgeLimit() const;
    QStringList parentalKeywords() const;
    int parentalAutoLockMinutes() const;
    int hiddenSelectionCount() const;
    QVariantList komgaGenres() const;
    // Each answers with parentalResult(ok, message).
    Q_INVOKABLE void unlockParental(const QString &pin);
    Q_INVOKABLE void enableParental(const QString &pin);
    Q_INVOKABLE void disableParental(const QString &pin);
    Q_INVOKABLE void lockParental();
    // Returns an error message, empty on success.
    Q_INVOKABLE QString setParentalRules(int ageLimit, const QStringList &keywords);
    Q_INVOKABLE void setParentalAutoLockMinutes(int minutes);
    Q_INVOKABLE void loadKomgaGenres();
    bool currentKoboMountable() const;
    // Kobos plugged in but never seen mounted: their name is not known yet.
    int unknownUnmountedCount() const;
    bool ejectAfterSync() const;
    void setEjectAfterSync(bool eject);
    bool seriesOnConnect() const;
    void setSeriesOnConnect(bool on);
    bool showSeriesSteps() const
    {
        return m_settings->showSeriesSteps();
    }
    void setShowSeriesSteps(bool show);
    bool lastDryRun() const
    {
        return m_lastDryRun;
    }
    int lastSeriesPending() const
    {
        return m_lastSeriesPending;
    }
    // Mounts the current Kobo, or with an empty serial the first unknown one; answers with
    // mountResult(ok, message).
    Q_INVOKABLE void mountKobo(const QString &serial);
    // Ejects the current Kobo; answers with mountResult(ok, message).
    Q_INVOKABLE void ejectCurrentKobo();
    Q_INVOKABLE void ejectKobo(const QString &serial);
    // Tests use a fake; nullptr goes back to Solid.
    void setVolumeBackend(VolumeBackend *backend);

    VariantListModel *shelves() const;
    bool hasShelves() const;
    bool shelfScanning() const;
    QString shelfScanStatus() const;
    // Each returns an error message, empty on success; adding a shelf scans it.
    Q_INVOKABLE QString addShelf(const QString &name, const QUrl &folder);
    // Creates the Kobold Library in the documents folder (SPEC-SHELF.md §2.1); returns an error.
    Q_INVOKABLE QString createKoboldLibrary();
    // The books of a shelf series, for editing every volume at once.
    Q_INVOKABLE QStringList seriesBookIds(const QString &seriesId) const;
    Q_INVOKABLE QString koboldLibraryParent() const;
    Q_INVOKABLE QString renameShelf(qint64 shelfId, const QString &name);
    Q_INVOKABLE void removeShelf(qint64 shelfId);
    // Scans one shelf, or every shelf with -1.
    Q_INVOKABLE void scanShelf(qint64 shelfId);
    // Tests use their own shelf database.
    void setShelfDatabase(std::unique_ptr<ShelfDatabase> database);

    QVariantList removalQuestions() const;
    VariantListModel *keptBooks() const;
    // answers: book id → true to remove; a missing answer takes the default.
    Q_INVOKABLE void answerRemovals(const QVariantMap &answers);
    Q_INVOKABLE void cancelRemovals();
    // Returns an error message, empty on success.
    Q_INVOKABLE QString removeKeptBook(const QString &bookId);

    // Tests keep the PIN in memory instead of the keychain.
    void setParentalPinStore(ApiKeyStore *pinStore);
    // Names left out of operation output while locked.
    HiddenContent hiddenContent() const;
    QStringList comicProfiles() const;

Q_SIGNALS:
    void komgaChanged();
    void currentKoboChanged();
    void filtersChanged();
    void libraryChanged();
    void currentItemChanged();
    void selectionChanged();
    void busyChanged();
    void coverStampChanged();
    void progressChanged();
    void diagnosticsChanged();
    void coverCacheChanged();
    void koboSyncChanged();
    void appIconChanged();
    void parentalChanged();
    void parentalResult(bool ok, const QString &message);
    // A sync waits for answers to removalQuestions.
    void removalsToConfirm();
    void shelvesChanged();
    void mountResult(bool ok, const QString &message);
    // After setting the series on a Kobo's connection: what to tell the user.
    void seriesSetOnConnect(bool ok, const QString &message);
    // Komga refused to create a key with the API key: ask for the email and password.
    void komgaCredentialsNeeded();
    void apiKeyResult(bool ok, const QString &message);

private:
    void setKomgaState(const QString &state, const QString &message);
    void reloadSelection();
    void refreshViews();
    void rebuildSeries();
    void rebuildGroups();
    void rebuildBooks();
    void rebuildSidebar();
    void collectCollectionBooks(int generation, QList<KomgaSeries> series, qsizetype index);
    void rebuildSelection();
    // Saves the store and resolves the selection again; returns an error message.
    QString saveSelection();
    void runOperation(Runner *runner, std::function<void(Runner *)> start);
    void runSyncChain(QStringList mountPoints, bool dryRun);
    // Checks the Komga sync endpoint of a connected Kobo once per connection (§6).
    void checkKoboSync(const QString &serial);
    // Window icon and About logo; with updateLauncher, also the copy the launcher uses.
    void applyAppIcon(bool updateLauncher);
    // True when this series is hidden right now.
    bool hidesSeries(const QString &seriesId) const;
    void updateAdultSeries();
    // Komga's libraries and series, then the shelves', into the browsing lists.
    void mergeShelves();
    // Series of each read list's books, read only while parental control is on, to hide read
    // lists whose books are all adult (SPEC-PARENTAL.md §4).
    void loadReadListSeries();
    void rebuildShelves();
    void scanNextShelf();
    void rebuildKeptBooks();
    void onParentalChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QString libraryName(const QString &libraryId) const;

    Settings *m_settings;
    ApiKeyStore *m_apiKeyStore;
    QNetworkAccessManager *m_network;
    KomgaClient *m_client;
    ThumbnailCache *m_thumbnails;

    QString m_komgaState = QStringLiteral("connecting");
    QString m_komgaMessage;
    QString m_userEmail;

    VariantListModel *m_kobos;
    QString m_currentKobo;
    QString m_currentKoboName;
    QString m_currentKoboMount;

    VariantListModel *m_libraries;
    QString m_libraryFilter;
    QString m_seriesSearch;
    bool m_libraryLoading = false;
    QList<KomgaLibrary> m_libraryList;
    QList<KomgaSeries> m_allSeries;
    QList<KomgaCollection> m_allCollections;
    QList<KomgaReadList> m_allReadLists;
    VariantListModel *m_series;
    VariantListModel *m_collections;
    VariantListModel *m_readLists;

    VariantListModel *m_sidebar;
    QStringList m_collapsedSections;

    VariantListModel *m_books;
    QString m_currentKind;
    QString m_currentId;
    KomgaSeries m_currentSeries;
    KomgaCollection m_currentCollection;
    KomgaReadList m_currentReadList;
    QList<KomgaBook> m_seriesBooks;
    int m_openGeneration = 0;
    bool m_booksLoading = false;

    std::unique_ptr<SelectionStore> m_store;
    ResolvedSelection m_resolved;
    // Series of the direct books, from their metadata.
    QHash<QString, KomgaBook> m_directBooks;
    QString m_selectionError;
    int m_selectionGeneration = 0;
    // Kobo the resolved groups belong to; they stay shown while a reload resolves again.
    QString m_resolvedKobo;
    // True from a reload until its books are known: unknown books may be adult ones.
    bool m_selectionPending = false;
    VariantListModel *m_selectionGroups;
    VariantListModel *m_selectionBooks;

    void resetProgress();
    bool m_busy = false;
    QString m_operationTitle;
    bool m_lastDryRun = false;
    int m_lastSeriesPending = 0;
    QString m_currentStep;
    int m_progressDone = 0;
    int m_progressTotal = 0;
    VariantListModel *m_log;
    QStringList m_failures;
    int m_lastExitCode = 0;
    bool m_lastSyncWrote = false;

    std::unique_ptr<SolidVolumeBackend> m_solidVolumes;
    KoboMounter *m_mounter = nullptr;
    KoboBooksController *m_koboBooks = nullptr;
    ShelfEditController *m_shelfEditor = nullptr;
    int m_coverStamp = 0;
    // Tests: where the Kobold Library goes instead of the documents folder.
    QString m_koboldLibraryParent;

public:
    // After an edit of shelf metadata: covers, sidebar and content shown again.
    void shelvesEdited(const QStringList &bookIds, const QStringList &seriesIds);
    void setKoboldLibraryParent(const QString &parent)
    {
        m_koboldLibraryParent = parent;
    }

private:
    // Polls the Kobos and reloads them when one appears, changes or goes.
    static constexpr int KobosRefreshMs = 3000;
    QTimer *m_kobosTimer = nullptr;
    QString m_kobosSignature;
    QString kobosSignature() const;
    static QString kobosSignature(const QList<ConnectedKobo> &connected, const QList<UnmountedKobo> &unmounted);
    void ejectMountPoint(const QString &mountPoint);
    QList<UnmountedKobo> m_unmounted;
    QHash<QString, QSet<QString>> m_readListSeries;
    bool m_readListSeriesLoaded = false;
    QList<KomgaLibrary> m_komgaLibraries;
    QList<KomgaSeries> m_komgaSeries;
    std::unique_ptr<ShelfDatabase> m_shelfDatabase;
    std::unique_ptr<ShelfLibrary> m_shelfLibrary;
    VariantListModel *m_shelves = nullptr;
    QList<qint64> m_shelvesToScan;
    ShelfScanner *m_shelfScanner = nullptr;
    QString m_shelfScanStatus;
    SyncRunner *m_activeSync = nullptr;
    QList<RemovalQuestion> m_removalQuestions;
    VariantListModel *m_keptBooks = nullptr;
    VariantListModel *m_localFiles = nullptr;
    ParentalLock *m_parental = nullptr;
    QSet<QString> m_adultSeries;
    int m_hiddenSelectionCount = 0;
    QStringList m_komgaGenres;
    QHash<QString, KoboSyncStatus> m_koboSyncStatus;
    // "<serial>@<mount point>" of the connections already checked.
    QSet<QString> m_koboSyncChecked;
    // Connections (serial@mount point) whose series were already set (SPEC-KOBO-BOOKS.md §3.2).
    QSet<QString> m_seriesChecked;
    void setSeriesOnConnect(const QString &serial, const QString &name, const QString &mountPoint);
    KoboSyncRunner *m_koboSyncRunner = nullptr;
    QString m_koboSyncResult;
    bool m_koboSyncResultOk = true;

    VariantListModel *m_diagnostics;
    bool m_diagnosticsRunning = false;
};

} // namespace Kobold
