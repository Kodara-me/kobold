// SPDX-License-Identifier: MIT

#pragma once

#include "komgaclient.h"
#include "manifest.h"
#include "parental.h"
#include "syncplanner.h"

#include <QObject>
#include <QSet>
#include <QStringList>

#include <memory>

class QTemporaryDir;

namespace Kobold
{

class ComicConverter;
class Converter;

// A removal the user confirms before it happens (SPEC-UNSYNC.md §2–§3).
struct RemovalQuestion {
    enum class Case {
        // 1: the book left the selection; Komga still has it.
        Deselected = 1,
        // 2: a local file left the selection; the computer still has it (SPEC-LOCAL.md §5).
        LocalDeselected = 2,
        // 3: Komga no longer has the book; the Kobo copy is the only one.
        GoneFromKomga = 3,
        // 4: a selected local file is gone from the computer; the Kobo copy is the only one.
        LocalMissing = 4,
    };

    Case kind = Case::Deselected;
    QString bookId;
    // Path on the device.
    QString path;
    // Book label when Komga still knows the book, otherwise the file name.
    QString label;
    QString seriesId;
    qint64 sizeBytes = 0;

    // The answer when the user just confirms: Remove for cases 1 and 2, Keep for 3 and 4.
    bool removeByDefault() const
    {
        return kind == Case::Deselected || kind == Case::LocalDeselected;
    }
    // Cases 3 and 4: the Kobo has the only copy.
    bool onlyCopy() const
    {
        return !removeByDefault();
    }
};

struct SyncReport {
    enum class Outcome {
        // The plan ran (or was printed, for a dry run). Individual books may have failed.
        Completed,
        // The manifest could not be read: nothing was changed.
        ManifestError,
        // The manifest belongs to another Komga server: nothing was changed.
        KomgaUrlMismatch,
        // Another fatal error before any change (e.g. temporary directory).
        Error,
        // The user cancelled a removal question: nothing was changed.
        Cancelled,
    };

    Outcome outcome = Outcome::Completed;
    QString errorString;
    QList<SyncAction> plan;
    int added = 0;
    int updated = 0;
    int removed = 0;
    int skipped = 0;
    int failed = 0;
    // Books marked kept on the user's answer.
    int kept = 0;
    // Books whose series was set in the Kobo's library (SPEC-KOBO-BOOKS.md §3.2).
    int seriesSet = 0;
    // Books copied by this sync with a series: the Kobo groups them only after importing them,
    // so they need a reconnection (SPEC-KOBO-BOOKS.md §3.2).
    int seriesPending = 0;
    // Removal questions: asked, or listed by a dry run.
    QList<RemovalQuestion> questions;
    // Books left on the Kobo for this run only, when nobody could answer.
    QStringList leftForNow;
    // Local files gone from the computer and removed from the Kobo: the caller takes them out
    // of the selection (SPEC-LOCAL.md §5).
    QStringList localFilesToUnselect;
    // "<book>: <cause>" for each failed book.
    QStringList failures;
};

// Removes a book Kobold placed (file and manifest entry) at once: "Remove Now" for a kept
// book (SPEC-UNSYNC.md §4). Returns false with errorString on failure.
bool removeManagedBook(const QString &deviceRoot, const QString &bookId, QString *errorString);

// Human-readable book name: "Series — Title", or the title alone.
QString bookLabel(const KomgaBook &book);

// Executes a sync (SPEC.md §9): fetches metadata, computes the plan, then applies it,
// saving the manifest after every completed action.
class SyncExecutor : public QObject
{
    Q_OBJECT

public:
    struct Options {
        QString deviceRoot;
        // The Kobo's serial number, for its backups; without it the Kobo's library is not
        // changed (no series set, SPEC-KOBO-BOOKS.md §3.2).
        QString serial;
        // Backups folder; the default one when empty (tests set it).
        QString backupsRoot;
        QString booksFolder;
        QString komgaUrl;
        QStringList selection;
        bool dryRun = false;
        // Accept a manifest written for another Komga URL and rewrite it.
        bool forceUrlChange = false;
        // KCC profile for comics on this device, or empty with the reason comics are skipped
        // (SPEC-COMICS.md §6).
        QString comicProfile;
        QString comicUnavailableReason;
        // SyncPlanInput::epubSyncedByKomga.
        bool epubSyncedByKomga = false;
        // Books whose names progress messages leave out (SPEC-PARENTAL.md §4).
        HiddenContent hidden;

        // How removals are decided (SPEC-UNSYNC.md): Ask pauses with removalsToConfirm();
        // Automatic uses the answers below, given in advance.
        enum class Removals {
            Automatic,
            Ask,
        };
        Removals removals = Removals::Automatic;
        bool removeDeselected = true;
        bool removeMissing = false;
        // Kept books to remove now; already confirmed.
        QSet<QString> removeNow;
        // Selected files on the computer (SPEC-LOCAL.md).
        QStringList localFiles;
    };

    static constexpr int MaxConcurrentRequests = 4;
    static constexpr qint64 FreeSpaceMarginBytes = 1024 * 1024;

    SyncExecutor(KomgaClient *client, Converter *converter, QObject *parent = nullptr);
    ~SyncExecutor() override;

    // Converter for comics; without one, comics need Options::comicProfile to be empty.
    void setComicConverter(ComicConverter *comicConverter);

    // Starts the sync. finished() is emitted exactly once.
    void run(const Options &options);

    // Answers to removalsToConfirm(): book id → remove. A missing answer takes the default.
    // Keep marks the book kept.
    void answerRemovals(const QHash<QString, bool> &remove);
    // Stops the sync before any change.
    void cancelRemovals();

Q_SIGNALS:
    void progress(const QString &message);
    // Before each action of the plan: actions done so far and their number.
    void stepped(int done, int total);
    // With Removals::Ask, before any change; the sync waits for answerRemovals() or
    // cancelRemovals().
    void removalsToConfirm(const QList<Kobold::RemovalQuestion> &questions);
    void finished(const Kobold::SyncReport &report);

private:
    void fetchNextBooks();
    void fetchSeriesDirections();
    void startPlan();
    // Turns the answers into actions, then runs the plan.
    void applyRemovalAnswers(const QHash<QString, bool> &remove, bool markKept);
    void startExecution();
    void executeKeep(const SyncAction &action);
    void executeNext();
    void executeRemove(const SyncAction &action);
    void executeRename(const SyncAction &action);
    void executeTransfer(const SyncAction &action);
    // Copies to the Kobo off the main thread, records the book, then calls done.
    void installFile(const SyncAction &action, const QString &sourcePath, std::function<void()> done);
    // Writes the series (and, for a shelf book, title and authors) into an EPUB copy off the
    // main thread. On error a shelf book fails (done(false)); another goes as it is.
    void writeKoboMetadata(const KomgaBook &book, const QString &epubPath, const QString &label, std::function<void(bool)> done);
    // After the plan: the series of Kobold's books in the Kobo's library, then finish().
    void setKoboSeries();
    void bookFailed(const QString &label, const QString &message);
    bool saveManifest(const QString &label);
    void finish();

    KomgaClient *m_client;
    Converter *m_converter;
    ComicConverter *m_comicConverter = nullptr;
    Options m_options;
    std::unique_ptr<Manifest> m_manifest;
    std::unique_ptr<QTemporaryDir> m_tempDir;
    QHash<QString, KomgaBook> m_books;
    QHash<QString, QString> m_unavailable;
    // Books Komga answered "not found" for, and metadata of books planned for removal.
    QSet<QString> m_missing;
    QHash<QString, KomgaBook> m_otherBooks;
    QStringList m_fetchQueue;
    // The selection with the ids of local files, and their paths on the computer.
    QStringList m_planSelection;
    QHash<QString, QString> m_localPaths;
    bool m_waitingForAnswers = false;
    QSet<QString> m_fetchFailures;
    // Reading direction of selected comics by book id (SPEC-COMICS.md §5.1).
    QHash<QString, QString> m_comicDirections;
    qsizetype m_nextFetch = 0;
    int m_inFlight = 0;
    qsizetype m_nextAction = 0;
    SyncReport m_report;
};

} // namespace Kobold
