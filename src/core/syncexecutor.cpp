// SPDX-License-Identifier: MIT

#include "syncexecutor.h"
#include "comicconverter.h"
#include "converter.h"
#include "epubmetadatawriter.h"
#include "filenaming.h"
#include "kobobooks.h"
#include "localfiles.h"
#include "logging.h"
#include "shelflibrary.h"

#include <KLocalizedString>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QLocale>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTimer>

#include "platform.h"

namespace Kobold
{

namespace
{
constexpr qint64 CopyChunkBytes = 1024 * 1024;

// Copies src to dst and flushes it to the device before returning.
bool copyFileDurably(const QString &sourcePath, const QString &destinationPath, QString *errorString)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        *errorString = i18n("cannot read %1: %2", sourcePath, source.errorString());
        return false;
    }
    QFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *errorString = i18n("cannot write %1: %2", destinationPath, destination.errorString());
        return false;
    }
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(CopyChunkBytes);
        if (chunk.isEmpty() && source.error() != QFileDevice::NoError) {
            *errorString = i18n("cannot read %1: %2", sourcePath, source.errorString());
            return false;
        }
        if (destination.write(chunk) != chunk.size()) {
            *errorString = i18n("cannot write %1: %2", destinationPath, destination.errorString());
            destination.close();
            QFile::remove(destinationPath);
            return false;
        }
    }
    if (!destination.flush() || !Platform::syncFile(destination)) {
        *errorString = i18n("cannot flush %1 to the device", destinationPath);
        destination.close();
        QFile::remove(destinationPath);
        return false;
    }
    destination.close();
    return true;
}

// Runs work on the thread pool, then then(result) on context's thread. work must not use
// context: the executor can be gone before it ends.
template<typename Work, typename Then>
void runInBackground(QObject *context, Work work, Then then)
{
    using Result = std::invoke_result_t<Work>;
    auto *watcher = new QFutureWatcher<Result>(context);
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, context, [watcher, then = std::move(then)]() mutable {
        watcher->deleteLater();
        then(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}

// Renames a file without replacing an existing one.
bool moveFile(const QString &from, const QString &to, QString *errorString)
{
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (!QFileInfo::exists(to) && QFile::rename(from, to)) {
        return true;
    }
    *errorString = i18n("cannot rename %1 to %2", from, to);
    return false;
}

QString sourceSuffix(const KomgaBook &book)
{
    if (book.mediaType == MediaTypePdf) {
        return QStringLiteral("pdf");
    }
    if (book.mediaType == QLatin1String("application/zip")) {
        return QStringLiteral("cbz");
    }
    if (isComicMediaType(book.mediaType)) {
        return QStringLiteral("cbr");
    }
    return QStringLiteral("epub");
}
}

bool removeManagedBook(const QString &deviceRoot, const QString &bookId, QString *errorString)
{
    Manifest manifest(deviceRoot);
    if (!manifest.load()) {
        *errorString = manifest.errorString();
        return false;
    }
    const ManifestEntry *entry = manifest.findByBookId(bookId);
    if (!entry) {
        return true;
    }
    const QString path = QDir(deviceRoot).filePath(entry->path);
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        *errorString = i18n("cannot delete %1", entry->path);
        return false;
    }
    const QString hiddenPath = KoboBooks::hiddenFiles(deviceRoot).value(entry->path);
    if (!hiddenPath.isEmpty()) {
        QFile::remove(QDir(deviceRoot).filePath(hiddenPath));
    }
    manifest.remove(bookId);
    if (!manifest.save()) {
        *errorString = manifest.errorString();
        return false;
    }
    return true;
}

QString bookLabel(const KomgaBook &book)
{
    const QString title = book.title.isEmpty() ? book.name : book.title;
    if (book.seriesTitle.isEmpty() || book.seriesTitle == title) {
        return title;
    }
    return book.seriesTitle + QStringLiteral(" — ") + title;
}

SyncExecutor::SyncExecutor(KomgaClient *client, Converter *converter, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_converter(converter)
{
}

SyncExecutor::~SyncExecutor() = default;

void SyncExecutor::setComicConverter(ComicConverter *comicConverter)
{
    m_comicConverter = comicConverter;
}

void SyncExecutor::run(const Options &options)
{
    m_options = options;
    m_report = SyncReport();
    m_books.clear();
    m_unavailable.clear();
    m_missing.clear();
    m_otherBooks.clear();
    m_localPaths.clear();
    m_waitingForAnswers = false;
    m_fetchFailures.clear();
    m_comicDirections.clear();
    m_nextFetch = 0;
    m_inFlight = 0;
    m_nextAction = 0;

    m_manifest = std::make_unique<Manifest>(options.deviceRoot);
    if (!m_manifest->load()) {
        m_report.outcome = SyncReport::Outcome::ManifestError;
        m_report.errorString = m_manifest->errorString();
        finish();
        return;
    }
    if (!m_manifest->komgaUrl().isEmpty() && !isSameKomgaUrl(m_manifest->komgaUrl(), options.komgaUrl)) {
        if (!options.forceUrlChange) {
            m_report.outcome = SyncReport::Outcome::KomgaUrlMismatch;
            m_report.errorString =
                i18n("the Kobo was synced from %1, not %2; use --force-url-change to switch servers", m_manifest->komgaUrl(), options.komgaUrl);
            finish();
            return;
        }
    }
    const bool urlChanged = m_manifest->komgaUrl() != options.komgaUrl;
    m_manifest->setKomgaUrl(options.komgaUrl);
    if (urlChanged && !m_manifest->entries().isEmpty() && !options.dryRun && !m_manifest->save()) {
        m_report.outcome = SyncReport::Outcome::ManifestError;
        m_report.errorString = m_manifest->errorString();
        finish();
        return;
    }

    // Local files are described from the disk; a missing one is case 4 (SPEC-LOCAL.md §5).
    m_planSelection = options.selection;
    for (const QString &path : options.localFiles) {
        const QString id = LocalFiles::idFor(path);
        m_localPaths.insert(id, path);
        m_planSelection.append(id);
        if (const auto book = LocalFiles::describe(path)) {
            m_books.insert(id, *book);
        } else {
            m_missing.insert(id);
            m_unavailable.insert(id, i18n("missing on the computer"));
        }
    }
    for (const ManifestEntry &entry : m_manifest->entries()) {
        if (LocalFiles::isLocalId(entry.komgaBookId) && !m_localPaths.contains(entry.komgaBookId)) {
            m_localPaths.insert(entry.komgaBookId, entry.localPath);
        }
    }

    // The selection, and the books that would leave the Kobo: whether Komga still has them
    // decides the question asked (SPEC-UNSYNC.md §2).
    m_fetchQueue = options.selection;
    const QSet<QString> selected(options.selection.cbegin(), options.selection.cend());
    for (const ManifestEntry &entry : m_manifest->entries()) {
        if (!entry.kept && !selected.contains(entry.komgaBookId) && !LocalFiles::isLocalId(entry.komgaBookId)) {
            m_fetchQueue.append(entry.komgaBookId);
        }
    }
    if (m_fetchQueue.isEmpty()) {
        startPlan();
        return;
    }
    Q_EMIT progress(i18np("Fetching metadata of 1 book from Komga…", "Fetching metadata of %1 books from Komga…", m_fetchQueue.size()));
    fetchNextBooks();
}

void SyncExecutor::fetchNextBooks()
{
    while (m_inFlight < MaxConcurrentRequests && m_nextFetch < m_fetchQueue.size()) {
        const bool isSelected = m_nextFetch < m_options.selection.size();
        const QString bookId = m_fetchQueue.at(m_nextFetch++);
        ++m_inFlight;
        m_client->fetchBook(bookId, [this, bookId, isSelected](const KomgaResult<KomgaBook> &result) {
            --m_inFlight;
            if (result.error.kind == KomgaError::Kind::NotFound) {
                m_missing.insert(bookId);
            }
            if (!isSelected) {
                if (result.ok()) {
                    m_otherBooks.insert(bookId, result.value);
                }
            } else if (result.ok()) {
                m_books.insert(bookId, result.value);
            } else {
                m_unavailable.insert(bookId, result.error.message);
                if (result.error.kind != KomgaError::Kind::NotFound) {
                    m_fetchFailures.insert(bookId);
                }
            }
            if (m_inFlight == 0 && m_nextFetch >= m_fetchQueue.size()) {
                fetchSeriesDirections();
            } else {
                fetchNextBooks();
            }
        });
    }
}

void SyncExecutor::fetchSeriesDirections()
{
    // The direction only matters for comics that can be converted.
    QHash<QString, QStringList> comicsBySeries;
    if (m_comicConverter && !m_options.comicProfile.isEmpty()) {
        for (const KomgaBook &book : std::as_const(m_books)) {
            // Local comics have no series: they are read left to right.
            if (isComicMediaType(book.mediaType) && !LocalFiles::isLocalId(book.id)) {
                comicsBySeries[book.seriesId].append(book.id);
            }
        }
    }
    if (comicsBySeries.isEmpty()) {
        startPlan();
        return;
    }

    auto pending = std::make_shared<qsizetype>(comicsBySeries.size());
    for (auto it = comicsBySeries.cbegin(); it != comicsBySeries.cend(); ++it) {
        m_client->fetchOneSeries(it.key(), [this, pending, bookIds = it.value()](const KomgaResult<KomgaSeries> &series) {
            for (const QString &bookId : bookIds) {
                if (series.ok()) {
                    m_comicDirections.insert(bookId, series.value.isRightToLeft() ? QString(DirectionRightToLeft) : QString(DirectionLeftToRight));
                } else {
                    // Never convert with a guessed direction.
                    m_books.remove(bookId);
                    m_unavailable.insert(bookId, i18n("cannot read the series reading direction: %1", series.error.message));
                    m_fetchFailures.insert(bookId);
                }
            }
            if (--*pending == 0) {
                startPlan();
            }
        });
    }
}

void SyncExecutor::startPlan()
{
    const QDir device(m_options.deviceRoot);

    SyncPlanInput input;
    input.selection = m_planSelection;
    input.books = m_books;
    input.unavailable = m_unavailable;
    input.manifestEntries = m_manifest->entries();
    input.booksFolder = m_options.booksFolder;
    input.comicProfile = m_comicConverter ? m_options.comicProfile : QString();
    input.epubSyncedByKomga = m_options.epubSyncedByKomga;
    input.comicUnavailableReason = m_options.comicUnavailableReason;
    input.comicDirections = m_comicDirections;
    input.missingFromKomga = m_missing;
    input.removeNow = m_options.removeNow;
    input.adoptExisting = m_manifest->wasEmpty();
    if (m_manifest->wasEmpty()) {
        Q_EMIT progress(i18n("Kobold's record on this Kobo was empty (the Kobo was probably unplugged while it was written); "
                             "its books found in their folder are taken back and copied again."));
    }

    const QDir folder(device.filePath(m_options.booksFolder));
    const QStringList files = folder.entryList(QDir::Files | QDir::Hidden | QDir::System);
    for (const QString &file : files) {
        input.existingPaths.insert(device.relativeFilePath(folder.filePath(file)));
    }
    const QDir localFolder(device.filePath(input.localFolder));
    const QStringList localFiles = localFolder.entryList(QDir::Files | QDir::Hidden | QDir::System);
    for (const QString &file : localFiles) {
        input.existingPaths.insert(device.relativeFilePath(localFolder.filePath(file)));
    }
    for (const ManifestEntry &entry : m_manifest->entries()) {
        if (QFileInfo(device.filePath(entry.path)).isFile()) {
            input.existingPaths.insert(entry.path);
        }
    }
    // A book hidden on the Kobo is still there (SPEC-KOBO-BOOKS.md §3).
    const QHash<QString, QString> hidden = KoboBooks::hiddenFiles(m_options.deviceRoot);
    for (auto it = hidden.cbegin(); it != hidden.cend(); ++it) {
        if (QFileInfo(device.filePath(it.value())).isFile()) {
            input.existingPaths.insert(it.key());
        }
    }

    m_report.plan = planSync(input);

    for (const SyncAction &action : std::as_const(m_report.plan)) {
        if (action.type == SyncAction::Type::Skip) {
            if (m_fetchFailures.contains(action.bookId)) {
                ++m_report.failed;
                m_report.failures.append(QStringLiteral("%1: %2").arg(action.bookId, action.reason));
            } else {
                ++m_report.skipped;
            }
        }
    }

    // Removals the user decides (SPEC-UNSYNC.md §2).
    for (SyncAction &action : m_report.plan) {
        if (action.type != SyncAction::Type::Remove || m_options.removeNow.contains(action.bookId)) {
            continue;
        }
        if (!action.book) {
            if (m_otherBooks.contains(action.bookId)) {
                action.book = m_otherBooks.value(action.bookId);
            } else if (m_books.contains(action.bookId)) {
                action.book = m_books.value(action.bookId);
            }
        }
        RemovalQuestion question;
        if (LocalFiles::isLocalId(action.bookId)) {
            question.kind = action.stillSelected ? RemovalQuestion::Case::LocalMissing : RemovalQuestion::Case::LocalDeselected;
        } else {
            question.kind = action.missingFromKomga ? RemovalQuestion::Case::GoneFromKomga : RemovalQuestion::Case::Deselected;
        }
        question.bookId = action.bookId;
        question.path = action.currentPath;
        if (LocalFiles::isLocalId(action.bookId)) {
            question.label = QDir::toNativeSeparators(m_localPaths.value(action.bookId, action.currentPath));
        } else {
            question.label = action.book ? bookLabel(*action.book) : QFileInfo(action.currentPath).completeBaseName();
        }
        question.seriesId = action.book ? action.book->seriesId : QString();
        question.sizeBytes = QFileInfo(device.filePath(action.currentPath)).size();
        m_report.questions.append(question);
    }
    // Ask in the order of the cases: 1 first, then 3.
    std::stable_sort(m_report.questions.begin(), m_report.questions.end(), [](const RemovalQuestion &a, const RemovalQuestion &b) {
        return int(a.kind) < int(b.kind);
    });

    if (m_options.dryRun) {
        finish();
        return;
    }
    if (m_report.questions.isEmpty()) {
        startExecution();
        return;
    }
    if (m_options.removals == Options::Removals::Ask) {
        m_waitingForAnswers = true;
        Q_EMIT removalsToConfirm(m_report.questions);
        return;
    }
    QHash<QString, bool> answers;
    for (const RemovalQuestion &question : std::as_const(m_report.questions)) {
        answers.insert(question.bookId, question.removeByDefault() ? m_options.removeDeselected : m_options.removeMissing);
    }
    applyRemovalAnswers(answers, false);
}

void SyncExecutor::answerRemovals(const QHash<QString, bool> &remove)
{
    if (!m_waitingForAnswers) {
        return;
    }
    m_waitingForAnswers = false;
    QHash<QString, bool> answers = remove;
    for (const RemovalQuestion &question : std::as_const(m_report.questions)) {
        if (!answers.contains(question.bookId)) {
            answers.insert(question.bookId, question.removeByDefault());
        }
    }
    applyRemovalAnswers(answers, true);
}

void SyncExecutor::cancelRemovals()
{
    if (!m_waitingForAnswers) {
        return;
    }
    m_waitingForAnswers = false;
    m_report.outcome = SyncReport::Outcome::Cancelled;
    finish();
}

void SyncExecutor::applyRemovalAnswers(const QHash<QString, bool> &remove, bool markKept)
{
    for (SyncAction &action : m_report.plan) {
        if (action.type != SyncAction::Type::Remove || !remove.contains(action.bookId)) {
            continue;
        }
        if (remove.value(action.bookId)) {
            // A local file gone from the computer leaves the selection with its Kobo copy.
            if (LocalFiles::isLocalId(action.bookId) && action.stillSelected) {
                m_report.localFilesToUnselect.append(m_localPaths.value(action.bookId));
            }
            continue;
        }
        if (markKept) {
            action.type = SyncAction::Type::Keep;
            action.reason = LocalFiles::isLocalId(action.bookId) ? (action.stillSelected ? QStringLiteral("missing on the computer") : QStringLiteral("deselected"))
                : action.missingFromKomga                    ? (ShelfLibrary::isShelfId(action.bookId) ? QStringLiteral("not on its shelf") : QStringLiteral("not in Komga"))
                                                             : QStringLiteral("deselected");
        } else {
            action.type = SyncAction::Type::Skip;
            action.reason = i18n("left on the Kobo for now");
            m_report.leftForNow.append(
                m_options.hidden.book(action.bookId, action.book ? action.book->seriesId : QString(), action.book ? bookLabel(*action.book) : action.currentPath));
        }
    }
    startExecution();
}

void SyncExecutor::startExecution()
{
    m_tempDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/kobold-XXXXXX"));
    if (!m_tempDir->isValid()) {
        m_report.outcome = SyncReport::Outcome::Error;
        m_report.errorString = i18n("cannot create a temporary directory: %1", m_tempDir->errorString());
        finish();
        return;
    }
    executeNext();
}

void SyncExecutor::executeNext()
{
    while (m_nextAction < m_report.plan.size()) {
        Q_EMIT stepped(int(m_nextAction), int(m_report.plan.size()));
        const SyncAction action = m_report.plan.at(m_nextAction++);
        switch (action.type) {
        case SyncAction::Type::Skip:
            continue;
        case SyncAction::Type::Keep:
            executeKeep(action);
            continue;
        case SyncAction::Type::Remove:
            executeRemove(action);
            continue;
        case SyncAction::Type::Update:
            if (!action.needsTransfer) {
                executeRename(action);
                continue;
            }
            executeTransfer(action);
            return;
        case SyncAction::Type::Add:
            executeTransfer(action);
            return;
        }
    }
    setKoboSeries();
}

void SyncExecutor::setKoboSeries()
{
    // The Kobo groups sideloaded books by content.SeriesID, which it leaves empty: Kobold sets
    // it for its books the Kobo already imported (SPEC-KOBO-BOOKS.md §3.2). Books copied now get
    // it at the next sync, once the Kobo has imported them.
    if (m_options.dryRun || m_options.serial.isEmpty()) {
        finish();
        return;
    }
    QHash<QString, KoboBooks::Series> series;
    bool manifestChanged = false;
    for (ManifestEntry entry : m_manifest->entries()) {
        const auto book = m_books.constFind(entry.komgaBookId);
        if (book == m_books.cend() || book->seriesTitle.isEmpty()) {
            continue;
        }
        series.insert(entry.path, KoboBooks::Series{book->seriesTitle, book->numberSort});
        // Books copied before the manifest kept series get them now, for the next connection.
        if (entry.series != book->seriesTitle || entry.seriesNumber != book->numberSort) {
            entry.series = book->seriesTitle;
            entry.seriesNumber = book->numberSort;
            m_manifest->upsert(entry);
            manifestChanged = true;
        }
    }
    if (manifestChanged) {
        saveManifest(QString());
    }
    if (series.isEmpty()) {
        finish();
        return;
    }
    const QString root = m_options.deviceRoot;
    const QString serial = m_options.serial;
    const QString backups = m_options.backupsRoot;
    runInBackground(
        this,
        [root, serial, backups, series]() {
            KoboBooks books(root, serial, backups.isEmpty() ? KoboBooks::defaultBackupsRoot() : backups);
            return books.setSeries(series);
        },
        [this](const KoboBooks::ApplyResult &result) {
            if (!result.ok) {
                qCWarning(KOBOLD_SYNC) << "series not set on the Kobo:" << result.errorString;
                Q_EMIT progress(i18n("The series could not be set in the Kobo's library: %1", result.errorString));
                finish();
                return;
            }
            // Every book with a series still missing from the Kobo's library, copied now or by
            // an earlier sync, waits for the next connection.
            m_report.seriesPending = result.pending;
            if (result.changed > 0) {
                m_report.seriesSet = result.changed;
                Q_EMIT progress(i18np("Series set for 1 book in the Kobo's library.", "Series set for %1 books in the Kobo's library.", result.changed));
            }
            finish();
        });
}

void SyncExecutor::executeRemove(const SyncAction &action)
{
    const QString path = QDir(m_options.deviceRoot).filePath(action.currentPath);
    Q_EMIT progress(i18n("Removing %1", m_options.hidden.book(action.bookId, QString(), action.currentPath)));
    if (QFileInfo::exists(path) && !QFile::remove(path)) {
        bookFailed(action.currentPath, i18n("cannot delete the file"));
        return;
    }
    const QString hiddenPath = KoboBooks::hiddenFiles(m_options.deviceRoot).value(action.currentPath);
    if (!hiddenPath.isEmpty()) {
        QFile::remove(QDir(m_options.deviceRoot).filePath(hiddenPath));
    }
    m_manifest->remove(action.bookId);
    if (saveManifest(action.currentPath)) {
        ++m_report.removed;
    }
}

void SyncExecutor::executeKeep(const SyncAction &action)
{
    const auto existing = m_manifest->findByBookId(action.bookId);
    if (!existing) {
        return;
    }
    ManifestEntry entry = *existing;
    entry.kept = true;
    entry.keptReason = action.reason;
    m_manifest->upsert(entry);
    if (saveManifest(action.currentPath)) {
        ++m_report.kept;
    }
}

void SyncExecutor::executeRename(const SyncAction &action)
{
    const QDir device(m_options.deviceRoot);
    const QString label = m_options.hidden.book(action.book->id, action.book->seriesId, bookLabel(*action.book));
    if (action.currentPath == action.targetPath) {
        Q_EMIT progress(i18n("%1 is selected again", label));
    } else if (m_options.hidden.hidesBook(action.bookId, action.book ? action.book->seriesId : QString())) {
        Q_EMIT progress(i18n("Renaming a hidden book"));
    } else {
        Q_EMIT progress(i18n("Renaming %1 to %2", action.currentPath, action.targetPath));
    }

    QString errorString;
    if (action.currentPath != action.targetPath && !moveFile(device.filePath(action.currentPath), device.filePath(action.targetPath), &errorString)) {
        bookFailed(label, errorString);
        return;
    }
    ManifestEntry entry = *m_manifest->findByBookId(action.bookId);
    entry.path = action.targetPath;
    entry.kept = false;
    entry.keptReason.clear();
    m_manifest->upsert(entry);
    if (saveManifest(label)) {
        ++m_report.updated;
    }
}

void SyncExecutor::executeTransfer(const SyncAction &action)
{
    const KomgaBook book = *action.book;
    const QString label = m_options.hidden.book(book.id, book.seriesId, bookLabel(book));
    const QString downloadPath = m_tempDir->filePath(QStringLiteral("%1.%2").arg(QString::number(m_nextAction), sourceSuffix(book)));

    // A local file is read from the computer instead of downloaded (SPEC-LOCAL.md §3).
    const auto fetchSource = [this, book, label, downloadPath](std::function<void(const KomgaError &)> done) {
        if (!LocalFiles::isLocalId(book.id)) {
            Q_EMIT progress(i18n("Downloading %1", label));
            m_client->downloadBookFile(book.id, downloadPath, std::move(done));
            return;
        }
        Q_EMIT progress(i18n("Reading %1", label));
        const QString source = m_localPaths.value(book.id);
        runInBackground(
            this,
            [source, downloadPath]() {
                return QFile::copy(source, downloadPath);
            },
            [source, done = std::move(done)](bool copied) {
                KomgaError error;
                if (!copied) {
                    error.kind = KomgaError::Kind::File;
                    error.message = i18n("cannot read %1", source);
                }
                done(error);
            });
    };
    fetchSource([this, action, book, label, downloadPath](const KomgaError &error) {
        if (error.isError()) {
            bookFailed(label, error.message);
            executeNext();
            return;
        }

        if (isComicMediaType(book.mediaType)) {
            const QString convertedPath = m_tempDir->filePath(QStringLiteral("%1.kepub.epub").arg(QString::number(m_nextAction)));
            const bool rightToLeft = m_comicDirections.value(book.id) == DirectionRightToLeft;
            Q_EMIT progress(rightToLeft ? i18n("Converting %1 with KCC (%2, right to left)", label, m_options.comicProfile)
                                        : i18n("Converting %1 with KCC (%2, left to right)", label, m_options.comicProfile));
            m_comicConverter->convert(downloadPath,
                                      convertedPath,
                                      m_options.comicProfile,
                                      book.title.isEmpty() ? book.name : book.title,
                                      FileNaming::primaryAuthor(book),
                                      rightToLeft,
                                      [this, action, label, downloadPath, convertedPath](const ComicConverter::Result &result) {
                                          QFile::remove(downloadPath);
                                          if (!result.ok) {
                                              bookFailed(label, result.errorString);
                                              QFile::remove(convertedPath);
                                              executeNext();
                                              return;
                                          }
                                          const auto done = [this, convertedPath]() {
                                              QFile::remove(convertedPath);
                                              executeNext();
                                          };
                                          writeKoboMetadata(*action.book, convertedPath, label, [this, action, convertedPath, done](bool ok) {
                                              if (!ok) {
                                                  done();
                                                  return;
                                              }
                                              installFile(action, convertedPath, done);
                                          });
                                      });
            return;
        }

        if (book.mediaType != MediaTypeEpub || book.epubIsKepub) {
            const bool epub = book.mediaType == MediaTypeEpub;
            const auto install = [this, action, downloadPath]() {
                installFile(action, downloadPath, [this, downloadPath]() {
                    QFile::remove(downloadPath);
                    executeNext();
                });
            };
            if (!epub) {
                install();
                return;
            }
            writeKoboMetadata(book, downloadPath, label, [this, downloadPath, install](bool ok) {
                if (!ok) {
                    QFile::remove(downloadPath);
                    executeNext();
                    return;
                }
                install();
            });
            return;
        }

        const QString convertedPath = m_tempDir->filePath(QStringLiteral("%1.kepub.epub").arg(QString::number(m_nextAction)));
        Q_EMIT progress(i18n("Converting %1", label));
        m_converter->convert(downloadPath, convertedPath, [this, action, book, label, downloadPath, convertedPath](const Converter::Result &result) {
            QFile::remove(downloadPath);
            const auto done = [this, convertedPath]() {
                QFile::remove(convertedPath);
                executeNext();
            };
            if (!result.ok) {
                bookFailed(label, result.errorString);
                done();
                return;
            }
            writeKoboMetadata(book, convertedPath, label, [this, action, convertedPath, done](bool ok) {
                if (!ok) {
                    done();
                    return;
                }
                installFile(action, convertedPath, done);
            });
        });
    });
}

void SyncExecutor::writeKoboMetadata(const KomgaBook &book, const QString &epubPath, const QString &label, std::function<void(bool)> done)
{
    // Shelf books carry all of Kobold's metadata into the copy (SPEC-SHELF.md §6); the others
    // only their series, which KCC and some EPUB files lack and the Kobo groups books by.
    const bool shelf = ShelfLibrary::isShelfId(book.id);
    EpubMetadata metadata;
    if (shelf) {
        metadata.title = book.title;
        for (const KomgaAuthor &author : book.authors) {
            metadata.authors.append(author.name);
        }
    }
    // Shelf one-shots ("…:o<id>") and local files have no series.
    if (!book.seriesTitle.isEmpty() && !(shelf && book.seriesId.section(QLatin1Char(':'), -1).startsWith(QLatin1Char('o')))) {
        metadata.series = book.seriesTitle;
        metadata.number = book.numberSort;
    }
    if (metadata.title.isEmpty() && metadata.series.isEmpty()) {
        done(true);
        return;
    }
    runInBackground(
        this,
        [epubPath, metadata]() {
            QString errorString;
            EpubMetadataWriter::write(epubPath, metadata, &errorString);
            return errorString;
        },
        [this, shelf, label, done = std::move(done)](const QString &errorString) {
            if (errorString.isEmpty()) {
                done(true);
            } else if (shelf) {
                bookFailed(label, errorString);
                done(false);
            } else {
                // The series is a bonus for other books: the copy goes as it is.
                qCWarning(KOBOLD_SYNC) << label << "series not written:" << errorString;
                Q_EMIT progress(i18n("The series could not be written into %1 (%2); the Kobo's library gets it at the next sync.", label, errorString));
                done(true);
            }
        });
}

void SyncExecutor::installFile(const SyncAction &action, const QString &sourcePath, std::function<void()> done)
{
    const KomgaBook book = *action.book;
    const QString label = m_options.hidden.book(book.id, book.seriesId, bookLabel(book));
    const QDir device(m_options.deviceRoot);
    const QString targetPath = device.filePath(action.targetPath);
    const QString partPath = targetPath + QStringLiteral(".part");
    const qint64 size = QFileInfo(sourcePath).size();

    // A file that appeared since planning and is not ours is never overwritten.
    const bool ownTarget = action.type == SyncAction::Type::Update && action.currentPath.compare(action.targetPath, Qt::CaseInsensitive) == 0;
    if (QFileInfo::exists(targetPath) && !ownTarget) {
        bookFailed(label, i18n("%1 already exists on the device", action.targetPath));
        done();
        return;
    }

    if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
        bookFailed(label, i18n("cannot create the folder for %1", action.targetPath));
        done();
        return;
    }
    const QStorageInfo storage(m_options.deviceRoot);
    if (storage.isValid() && storage.bytesAvailable() < size + FreeSpaceMarginBytes) {
        bookFailed(label, i18n("not enough free space on the device (%1 needed)", QLocale().formattedDataSize(size + FreeSpaceMarginBytes)));
        done();
        return;
    }

    Q_EMIT progress(i18n("Copying %1", m_options.hidden.book(action.bookId, action.book ? action.book->seriesId : QString(), action.targetPath)));
    // Writing to the Kobo is slow (USB, fsync): it runs off the main thread.
    const QString previousPath = action.type == SyncAction::Type::Update && !ownTarget ? device.filePath(action.currentPath) : QString();
    const QString relativeTarget = action.targetPath;
    runInBackground(
        this,
        [sourcePath, partPath, targetPath, previousPath, relativeTarget]() {
            QString errorString;
            if (!copyFileDurably(sourcePath, partPath, &errorString)) {
                QFile::remove(partPath);
                return errorString;
            }
            if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath)) {
                QFile::remove(partPath);
                return i18n("cannot replace %1", relativeTarget);
            }
            if (!QFile::rename(partPath, targetPath)) {
                QFile::remove(partPath);
                return i18n("cannot rename %1", partPath);
            }
            if (!previousPath.isEmpty()) {
                QFile::remove(previousPath);
            }
            return QString();
        },
        [this, action, book, label, size, done = std::move(done)](const QString &errorString) {
            if (!errorString.isEmpty()) {
                bookFailed(label, errorString);
                done();
                return;
            }
            ManifestEntry entry;
            entry.komgaBookId = book.id;
            entry.path = action.targetPath;
            entry.sourceMediaType = book.mediaType;
            entry.komgaFileHash = book.fileHash;
            entry.komgaFileLastModified = book.fileLastModified;
            entry.komgaSizeBytes = book.sizeBytes;
            entry.outputSizeBytes = size;
            entry.syncedAt = QDateTime::currentDateTimeUtc();
            entry.conversionProfile = isComicMediaType(book.mediaType) ? m_options.comicProfile : QString();
            entry.readingDirection = isComicMediaType(book.mediaType) ? m_comicDirections.value(book.id, DirectionLeftToRight) : QString();
            entry.localPath = m_localPaths.value(book.id);
            entry.series = book.seriesTitle;
            entry.seriesNumber = book.numberSort;
            m_manifest->upsert(entry);
            if (saveManifest(label)) {
                if (action.type == SyncAction::Type::Add) {
                    ++m_report.added;
                } else {
                    ++m_report.updated;
                }
                if (!book.seriesTitle.isEmpty()) {
                    ++m_report.seriesPending;
                }
            }
            done();
        });
}

void SyncExecutor::bookFailed(const QString &label, const QString &message)
{
    qCWarning(KOBOLD_SYNC) << label << message;
    ++m_report.failed;
    m_report.failures.append(QStringLiteral("%1: %2").arg(label, message));
}

bool SyncExecutor::saveManifest(const QString &label)
{
    if (m_manifest->save()) {
        return true;
    }
    bookFailed(label, m_manifest->errorString());
    return false;
}

void SyncExecutor::finish()
{
    m_tempDir.reset();
    Q_EMIT finished(m_report);
}

} // namespace Kobold
