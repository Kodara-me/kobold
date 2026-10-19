// SPDX-License-Identifier: MIT

#pragma once

#include "komgaclient.h"
#include "parental.h"
#include "progresscalculator.h"

#include <QList>
#include <QSet>
#include <QObject>
#include <QStringList>

#include <memory>
#include <optional>

class QTemporaryDir;

namespace Kobold
{

class Manifest;

struct ProgressReport {
    enum class Outcome {
        Completed,
        // The manifest could not be read.
        ManifestError,
        // KoboReader.sqlite could not be read.
        DatabaseError,
    };

    struct Change {
        QString bookId;
        QString label;
        ReadProgressUpdate update;
    };

    Outcome outcome = Outcome::Completed;
    QString errorString;
    // Updates sent, or that would be sent in a dry run.
    QList<Change> changes;
    int updated = 0;
    int unchanged = 0;
    // Sideloaded books on the Kobo that Kobold did not place.
    int unmatched = 0;
    // Books from shelves or the computer: no server to send their progress to (SPEC-SHELF.md §6).
    int withoutServer = 0;
    int failed = 0;
    QStringList failures;
};

// Sends reading progress from the Kobo to Komga (SPEC.md §11).
class ProgressSync : public QObject
{
    Q_OBJECT

public:
    struct Options {
        QString deviceRoot;
        bool dryRun = false;
        // Only these books, when set (SPEC-KOBO-SYNC.md §3).
        std::optional<QSet<QString>> onlyBooks;
        // Books whose names the report leaves out (SPEC-PARENTAL.md §4).
        HiddenContent hidden;
    };

    explicit ProgressSync(KomgaClient *client, QObject *parent = nullptr);
    ~ProgressSync() override;

    // finished() is emitted exactly once.
    void run(const Options &options);

Q_SIGNALS:
    void finished(const Kobold::ProgressReport &report);

private:
    struct Pending {
        QString bookId;
        KoboReadingState state;
    };

    void processNext();
    void bookDone();
    void finish();

    KomgaClient *m_client;
    Options m_options;
    std::unique_ptr<QTemporaryDir> m_workDir;
    QList<Pending> m_pending;
    qsizetype m_next = 0;
    int m_inFlight = 0;
    ProgressReport m_report;
};

} // namespace Kobold
