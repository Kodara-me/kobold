// SPDX-License-Identifier: MIT

#pragma once

#include "shelfdatabase.h"

#include <QObject>

#include <atomic>
#include <memory>

namespace Kobold
{

struct ShelfScanReport {
    bool ok = false;
    // The folder cannot be read (share not mounted): nothing changed (SPEC-SHELF.md §2).
    bool unavailable = false;
    bool cancelled = false;
    QString errorString;
    int added = 0;
    int updated = 0;
    int moved = 0;
    int missing = 0;
    int forgotten = 0;
    int unchanged = 0;
};

// Scans a shelf folder (SPEC-SHELF.md §3): files are read on a worker thread, only when new
// or changed; the database is updated on the caller's thread in one transaction.
class ShelfScanner : public QObject
{
    Q_OBJECT

public:
    static constexpr int DefaultForgetAfterDays = 30;

    explicit ShelfScanner(ShelfDatabase *database, QObject *parent = nullptr);
    ~ShelfScanner() override;

    // finished() is emitted exactly once.
    void scan(qint64 shelfId, int forgetAfterDays = DefaultForgetAfterDays);
    // Stops before the database changes.
    void cancel();

    // Fingerprint of a file: its size and the SHA-1 of its first MiB.
    static QString fingerprint(const QString &filePath);

Q_SIGNALS:
    void progress(int done, int total);
    void finished(const Kobold::ShelfScanReport &report);

private:
    ShelfDatabase *m_database;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

} // namespace Kobold

Q_DECLARE_METATYPE(Kobold::ShelfScanReport)
