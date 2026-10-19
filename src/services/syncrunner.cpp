// SPDX-License-Identifier: MIT

#include "syncrunner.h"

#include "exitcode.h"
#include "kobotarget.h"
#include "komgasession.h"

#include "converter.h"
#include "devicelocator.h"
#include "kobobooks.h"
#include "komgaclient.h"
#include "manifest.h"
#include "parentalfilter.h"
#include "progresssync.h"
#include "selectionresolver.h"
#include "selectionstore.h"
#include "shelflibrary.h"
#include "settings.h"
#include "syncexecutor.h"

#include <KLocalizedString>

namespace Kobold
{

SyncRunner::SyncRunner(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent)
    : Runner(parent)
    , m_settings(settings)
    , m_apiKeyStore(apiKeyStore)
    , m_network(network)
{
}

void SyncRunner::run(const Options &options)
{
    m_options = options;

    const KoboTarget target = resolveConnectedTarget(m_settings, options.kobo, options.devicePath);
    if (!target.ok) {
        fail(target.exitCode, target.errorString);
        return;
    }
    m_deviceRoot = target.mountPoint;
    m_serial = target.kobo.serial;
    m_selectionPath = target.selectionPath();
    emitLine(i18n("Kobo: %1 (%2)", target.kobo.name, target.kobo.serial));

    SelectionStore selection(m_selectionPath);
    if (!selection.load()) {
        fail(UsageError, selection.errorString());
        return;
    }

    checkConverter();
}

void SyncRunner::checkConverter()
{
    m_converter = new Converter(this);
    m_converter->setTimeout(m_settings->conversionTimeout());
    m_converter->probeVersion([this](const Converter::VersionResult &result) {
        if (!result.found) {
            fail(ToolMissing, i18n("%1; install it with: paru -S kepubify", result.errorString));
            return;
        }
        if (!result.supported) {
            fail(ToolMissing, result.errorString);
            return;
        }
        checkComics();
    });
}

void SyncRunner::checkComics()
{
    prepareComics(m_settings, m_deviceRoot, this, [this](const ComicSetup &setup) {
        m_comics = setup;
        connectToKomga();
    });
}

void SyncRunner::connectToKomga()
{
    openKomgaSession(
        m_settings,
        m_apiKeyStore,
        m_network,
        this,
        [this](KomgaClient *client) {
            m_client = client;
            if (!m_options.hideAdult) {
                resolveAndSync();
                return;
            }
            loadHiddenSeries(m_settings, m_client, false, [this](const QList<KomgaSeries> &adult) {
                m_options.hidden.seriesIds += seriesIds(adult);
                resolveAndSync();
            });
        },
        [this](int exitCode, const QString &message) {
            fail(exitCode, message);
        });
}

void SyncRunner::resolveAndSync()
{
    SelectionStore selection(m_selectionPath);
    if (!selection.load()) {
        fail(UsageError, selection.errorString());
        return;
    }
    m_localFiles = selection.localFiles();
    // Followed groups are resolved before any change; a group that cannot be read
    // stops the sync rather than removing its books (SPEC-SELECTION.md §7).
    resolveSelection(m_client, selection, [this](const ResolvedSelection &resolved) {
        if (!resolved.ok()) {
            fail(resolved.status == ResolvedSelection::Status::GroupMissing ? UsageError : KomgaFailure, resolved.errorString);
            return;
        }
        m_selection = resolved.bookIds;
        m_resolved = resolved;
        startSync();
    });
}

void SyncRunner::startSync()
{
    emitLine(i18n("Mount point: %1", m_deviceRoot));
    if (!m_options.dryRun && m_settings->backupBeforeSync(m_serial)) {
        const KoboBooks books(m_deviceRoot, m_serial);
        QString error;
        const QString backup = books.backup(&error);
        if (backup.isEmpty()) {
            fail(DeviceError, i18n("cannot back up the Kobo's database: %1", error));
            return;
        }
        books.prune();
        emitLine(i18n("Database backed up to %1", backup));
    }
    emitLine(i18n("Komga: %1 (%2 selected book(s))", m_settings->komgaUrl().toString(), m_selection.size()));
    if (!m_localFiles.isEmpty()) {
        emitLine(i18np("Local files: 1", "Local files: %1", m_localFiles.size()));
    }
    for (const ResolvedGroup &group : std::as_const(m_resolved.groups)) {
        const QString name = group.kind == ResolvedGroup::Kind::Series ? m_options.hidden.series(group.id, group.description()) : group.description();
        emitLine(i18np("  from the followed %2: 1 book", "  from the followed %2: %1 books", group.books.size(), name));
    }
    if (m_comics.profile.isEmpty()) {
        emitLine(i18n("Comics: skipped (%1)", m_comics.unavailableReason));
    } else {
        emitLine(i18n("Comics: KCC %1, profile %2 for %3", m_comics.kccVersion, m_comics.profile, modelDescription(m_comics.identity)));
    }

    if (m_settings->koboSyncInstalled(m_serial)) {
        emitLine(i18n("EPUB books: synced by Komga"));
        if (!m_options.dryRun) {
            sendProgressBeforeKomga();
            return;
        }
    }
    runExecutor();
}

void SyncRunner::sendProgressBeforeKomga()
{
    Manifest manifest(m_deviceRoot);
    QSet<QString> epubBooks;
    if (manifest.load()) {
        for (const ManifestEntry &entry : manifest.entries()) {
            if (entry.sourceMediaType == MediaTypeEpub) {
                epubBooks.insert(entry.komgaBookId);
            }
        }
    }
    if (epubBooks.isEmpty()) {
        runExecutor();
        return;
    }

    // Komga takes these books over: their reading progress must reach it before Kobold removes
    // its copies (SPEC-KOBO-SYNC.md §3).
    emitLine(i18np("Sending the reading progress of 1 EPUB book before Komga takes it over…",
                   "Sending the reading progress of %1 EPUB books before Komga takes them over…",
                   epubBooks.size()));
    auto *progress = new ProgressSync(m_client, this);
    connect(progress, &ProgressSync::finished, this, [this, progress](const ProgressReport &report) {
        progress->deleteLater();
        if (report.outcome != ProgressReport::Outcome::Completed || report.failed > 0) {
            for (const QString &failure : report.failures) {
                emitError(failure);
            }
            fail(KomgaFailure,
                 report.errorString.isEmpty() ? i18n("the reading progress could not be sent; EPUB books were left on the Kobo")
                                              : i18n("%1; EPUB books were left on the Kobo", report.errorString));
            return;
        }
        runExecutor();
    });
    progress->run(ProgressSync::Options{m_deviceRoot, false, epubBooks, m_options.hidden});
}

void SyncRunner::runExecutor()
{
    m_executor = new SyncExecutor(m_client, m_converter, this);
    connect(m_executor, &SyncExecutor::progress, this, [this](const QString &message) {
        emitLine(QStringLiteral("  ") + message);
    });
    connect(m_executor, &SyncExecutor::finished, this, &SyncRunner::report);
    connect(m_executor, &SyncExecutor::removalsToConfirm, this, &SyncRunner::removalsToConfirm);
    connect(m_executor, &SyncExecutor::stepped, this, &SyncRunner::stepped);

    SyncExecutor::Options options;
    options.deviceRoot = m_deviceRoot;
    options.serial = m_serial;
    options.booksFolder = m_settings->booksFolder();
    options.komgaUrl = m_settings->komgaUrl().toString();
    options.selection = m_selection;
    options.localFiles = m_localFiles;
    options.dryRun = m_options.dryRun;
    options.forceUrlChange = m_options.forceUrlChange;
    options.comicProfile = m_comics.profile;
    options.comicUnavailableReason = m_comics.unavailableReason;
    options.epubSyncedByKomga = m_settings->koboSyncInstalled(m_serial);
    options.removals = m_options.askRemovals ? SyncExecutor::Options::Removals::Ask : SyncExecutor::Options::Removals::Automatic;
    options.removeDeselected = m_options.removeDeselected;
    options.removeMissing = m_options.removeMissing;
    options.removeNow = m_options.removeNow;
    options.hidden = m_options.hidden;
    m_executor->setComicConverter(m_comics.converter);
    m_executor->run(options);
}

void SyncRunner::answerRemovals(const QHash<QString, bool> &remove)
{
    if (m_executor) {
        m_executor->answerRemovals(remove);
    }
}

void SyncRunner::cancelRemovals()
{
    if (m_executor) {
        m_executor->cancelRemovals();
    }
}

QString SyncRunner::hiddenPath(const SyncAction &action, const QString &text) const
{
    return m_options.hidden.book(action.bookId, action.book ? action.book->seriesId : QString(), text);
}

void SyncRunner::report(const SyncReport &report)
{
    if (report.outcome == SyncReport::Outcome::Cancelled) {
        emitLine(i18n("Sync cancelled; nothing was changed."));
        Q_EMIT finished(Success);
        return;
    }
    if (report.outcome != SyncReport::Outcome::Completed) {
        fail(UsageError, report.errorString);
        return;
    }

    if (m_options.dryRun) {
        if (report.plan.isEmpty()) {
            emitLine(i18n("Nothing to do: the Kobo is up to date."));
        } else {
            emitLine(i18n("Planned changes (dry run, nothing modified):"));
        }
        for (const SyncAction &action : report.plan) {
            switch (action.type) {
            case SyncAction::Type::Add:
                emitLine(QStringLiteral("  + %1").arg(hiddenPath(action, action.targetPath)));
                break;
            case SyncAction::Type::Update:
                if (action.needsTransfer) {
                    emitLine(QStringLiteral("  ~ %1 (%2)").arg(hiddenPath(action, action.targetPath), action.reason));
                } else {
                    emitLine(QStringLiteral("  > %1 -> %2").arg(hiddenPath(action, action.currentPath), hiddenPath(action, action.targetPath)));
                }
                break;
            case SyncAction::Type::Remove:
                // Removals the user decides are listed with their questions below.
                if (m_options.removeNow.contains(action.bookId)) {
                    emitLine(QStringLiteral("  - %1").arg(hiddenPath(action, action.currentPath)));
                }
                break;
            case SyncAction::Type::Skip:
                emitLine(QStringLiteral("  ! %1: %2").arg(hiddenPath(action, action.book ? bookLabel(*action.book) : action.bookId), action.reason));
                break;
            case SyncAction::Type::Keep:
                break;
            }
        }
        if (!report.questions.isEmpty()) {
            emitLine(i18n("Removals to confirm (default answer):"));
            for (const RemovalQuestion &question : report.questions) {
                const QString name = m_options.hidden.book(question.bookId, question.seriesId, question.label);
                switch (question.kind) {
                case RemovalQuestion::Case::Deselected:
                    emitLine(i18n("  - %1: left the selection; Komga still has it (Remove)", name));
                    break;
                case RemovalQuestion::Case::LocalDeselected:
                    emitLine(i18n("  - %1: left the selection; the computer still has it (Remove)", name));
                    break;
                case RemovalQuestion::Case::GoneFromKomga:
                    emitLine(ShelfLibrary::isShelfId(question.bookId) ? i18n("  - %1: no longer on its shelf; this is the only copy (Keep)", name)
                                                                      : i18n("  - %1: no longer in Komga; this is the only copy (Keep)", name));
                    break;
                case RemovalQuestion::Case::LocalMissing:
                    emitLine(i18n("  - %1: missing on the computer; this is the only copy (Keep)", name));
                    break;
                }
            }
        }
        Q_EMIT finished(report.failed > 0 ? PartialFailure : Success);
        return;
    }

    for (const SyncAction &action : report.plan) {
        if (action.type == SyncAction::Type::Skip && action.book) {
            emitLine(i18n("  Skipped %1: %2", hiddenPath(action, bookLabel(*action.book)), action.reason));
        }
    }
    for (const QString &failure : report.failures) {
        emitError(failure);
    }
    // Local files gone from the computer and removed from the Kobo leave the selection.
    if (!report.localFilesToUnselect.isEmpty()) {
        SelectionStore selection(m_selectionPath);
        if (selection.load()) {
            for (const QString &path : report.localFilesToUnselect) {
                selection.removeLocalFile(path);
                emitLine(i18n("Taken out of the selection: %1", path));
            }
            if (!selection.save()) {
                emitWarning(selection.errorString());
            }
        }
    }
    if (!report.leftForNow.isEmpty()) {
        emitLine(i18n("Left on the Kobo for now (pass --remove-deselected or --remove-missing to remove them):"));
        for (const QString &label : report.leftForNow) {
            emitLine(QStringLiteral("  ") + label);
        }
    }
    emitLine(i18n("Added %1, updated %2, removed %3, kept %4, skipped %5, failed %6.",
                  report.added,
                  report.updated,
                  report.removed,
                  report.kept,
                  report.skipped,
                  report.failed));
    m_changedKobo = report.added + report.updated + report.removed > 0;
    m_seriesPending = report.seriesPending;
    if (report.added + report.updated + report.removed > 0) {
        emitLine(i18n("Eject the Kobo so that it indexes the changes."));
    }
    Q_EMIT finished(report.failed > 0 ? PartialFailure : Success);
}

void SyncRunner::fail(int exitCode, const QString &message)
{
    emitError(message);
    Q_EMIT finished(exitCode);
}

} // namespace Kobold
