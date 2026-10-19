// SPDX-License-Identifier: MIT

#pragma once

#include "parental.h"
#include "runner.h"

#include <QObject>
#include <QString>

#include "comicsetup.h"
#include "selectionresolver.h"
#include "syncexecutor.h"

class QNetworkAccessManager;

namespace Kobold
{
class ApiKeyStore;
class Converter;
class KomgaClient;
class Settings;
struct SyncAction;
class SyncExecutor;
struct SyncReport;
}

namespace Kobold
{

// `kobold sync [--dry-run] [--force-url-change] [--device <path>]` (SPEC.md §9).
// Runs the checks first, then the sync, and prints the plan or a summary.
class SyncRunner : public Runner
{
    Q_OBJECT

public:
    struct Options {
        bool dryRun = false;
        bool forceUrlChange = false;
        // Empty: detect the Kobo.
        QString devicePath;
        // --kobo: name or serial of a connected Kobo.
        QString kobo;
        // Books and series whose names the output leaves out (SPEC-PARENTAL.md §4).
        HiddenContent hidden;
        // Find the adult series to leave out from the settings (the command line, unless
        // unlocked); the application passes hidden instead.
        bool hideAdult = false;
        // Removals (SPEC-UNSYNC.md): ask through removalsToConfirm(), or use the answers
        // given in advance.
        bool askRemovals = false;
        bool removeDeselected = true;
        bool removeMissing = false;
        // Kept books to remove now ("Remove Now"), already confirmed.
        QSet<QString> removeNow;
    };

    SyncRunner(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent = nullptr);

    // Emits finished() exactly once.
    void run(const Options &options);

    // Answers to removalsToConfirm() (SyncExecutor::answerRemovals), or stop before any change.
    void answerRemovals(const QHash<QString, bool> &remove);
    void cancelRemovals();
    // After a real sync: whether it changed the Kobo, and where the Kobo is mounted
    // (SPEC-MOUNT.md §4, eject after sync).
    bool changedKobo() const
    {
        return m_changedKobo;
    }
    // Books copied with a series, grouped by the Kobo only after a reconnection.
    int seriesPending() const
    {
        return m_seriesPending;
    }
    QString deviceRoot() const
    {
        return m_deviceRoot;
    }

    // Names left out of the output, known once the run has started.
    HiddenContent hiddenContent() const
    {
        return m_options.hidden;
    }

Q_SIGNALS:
    // With askRemovals, before any change; the sync waits for an answer.
    void removalsToConfirm(const QList<Kobold::RemovalQuestion> &questions);
    // Actions of the plan done so far, and their number.
    void stepped(int done, int total);

private:
    void checkConverter();
    void checkComics();
    void connectToKomga();
    void resolveAndSync();
    void startSync();
    // Sends the progress of EPUB books Komga takes over, then runs the sync.
    void sendProgressBeforeKomga();
    void runExecutor();
    // text, or "a hidden book" for a hidden book.
    QString hiddenPath(const SyncAction &action, const QString &text) const;
    void report(const SyncReport &report);
    void fail(int exitCode, const QString &message);

    Settings *m_settings;
    ApiKeyStore *m_apiKeyStore;
    QNetworkAccessManager *m_network;
    KomgaClient *m_client = nullptr;
    Converter *m_converter = nullptr;
    ComicSetup m_comics;
    ResolvedSelection m_resolved;
    SyncExecutor *m_executor = nullptr;
    Options m_options;
    QString m_deviceRoot;
    bool m_changedKobo = false;
    int m_seriesPending = 0;
    QString m_serial;
    QString m_selectionPath;
    QStringList m_selection;
    // Selected files on the computer (SPEC-LOCAL.md).
    QStringList m_localFiles;
};

} // namespace Kobold
