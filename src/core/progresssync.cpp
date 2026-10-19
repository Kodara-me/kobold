// SPDX-License-Identifier: MIT

#include "progresssync.h"
#include "localfiles.h"
#include "logging.h"
#include "manifest.h"
#include "shelflibrary.h"
#include "syncexecutor.h"

#include <QDir>
#include <QTemporaryDir>

namespace Kobold
{

namespace
{
constexpr int MaxConcurrentRequests = 4;
constexpr int ReadStatusUnread = 0;
}

ProgressSync::ProgressSync(KomgaClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

ProgressSync::~ProgressSync() = default;

void ProgressSync::run(const Options &options)
{
    m_options = options;
    m_report = ProgressReport();
    m_pending.clear();
    m_next = 0;
    m_inFlight = 0;

    Manifest manifest(options.deviceRoot);
    if (!manifest.load()) {
        m_report.outcome = ProgressReport::Outcome::ManifestError;
        m_report.errorString = manifest.errorString();
        finish();
        return;
    }

    m_workDir = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/kobold-XXXXXX"));
    const KoboDatabase::Result database = m_workDir->isValid() ? KoboDatabase::readSideloadedBooks(options.deviceRoot, m_workDir->path())
                                                               : KoboDatabase::Result{false, m_workDir->errorString(), {}};
    m_workDir.reset();
    if (!database.ok) {
        m_report.outcome = ProgressReport::Outcome::DatabaseError;
        m_report.errorString = database.errorString;
        finish();
        return;
    }

    for (const KoboReadingState &state : database.books) {
        const auto entry = std::find_if(manifest.entries().cbegin(), manifest.entries().cend(), [&state](const ManifestEntry &e) {
            return e.path.compare(state.path, Qt::CaseInsensitive) == 0;
        });
        if (entry == manifest.entries().cend()) {
            ++m_report.unmatched;
        } else if (options.onlyBooks && !options.onlyBooks->contains(entry->komgaBookId)) {
            continue;
        } else if (LocalFiles::isLocalId(entry->komgaBookId) || ShelfLibrary::isShelfId(entry->komgaBookId)) {
            ++m_report.withoutServer;
        } else if (state.readStatus == ReadStatusUnread) {
            ++m_report.unchanged;
        } else {
            m_pending.append(Pending{entry->komgaBookId, state});
        }
    }

    if (m_pending.isEmpty()) {
        finish();
        return;
    }
    processNext();
}

void ProgressSync::processNext()
{
    while (m_inFlight < MaxConcurrentRequests && m_next < m_pending.size()) {
        const Pending pending = m_pending.at(m_next++);
        ++m_inFlight;
        m_client->fetchBook(pending.bookId, [this, pending](const KomgaResult<KomgaBook> &result) {
            if (!result.ok()) {
                ++m_report.failed;
                m_report.failures.append(QStringLiteral("%1: %2").arg(pending.state.path, result.error.message));
                bookDone();
                return;
            }

            const KomgaBook &book = result.value;
            const auto update = computeProgressUpdate(pending.state, book);
            if (!update) {
                ++m_report.unchanged;
                bookDone();
                return;
            }

            const ProgressReport::Change change{book.id, m_options.hidden.book(book.id, book.seriesId, bookLabel(book)), *update};
            if (m_options.dryRun) {
                m_report.changes.append(change);
                bookDone();
                return;
            }

            m_client->updateReadProgress(book.id, update->page, update->completed, [this, change](const KomgaError &error) {
                if (error.isError()) {
                    ++m_report.failed;
                    m_report.failures.append(QStringLiteral("%1: %2").arg(change.label, error.message));
                } else {
                    ++m_report.updated;
                    m_report.changes.append(change);
                    qCDebug(KOBOLD_PROGRESS) << "updated" << change.bookId;
                }
                bookDone();
            });
        });
    }
}

void ProgressSync::bookDone()
{
    --m_inFlight;
    if (m_inFlight == 0 && m_next >= m_pending.size()) {
        finish();
    } else {
        processNext();
    }
}

void ProgressSync::finish()
{
    Q_EMIT finished(m_report);
}

} // namespace Kobold
