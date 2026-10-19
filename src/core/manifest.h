// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace Kobold
{

struct ManifestEntry {
    QString komgaBookId;
    // Relative to the device mount point, '/' separated.
    QString path;
    QString sourceMediaType;
    QString komgaFileHash;
    QDateTime komgaFileLastModified;
    qint64 komgaSizeBytes = 0;
    qint64 outputSizeBytes = 0;
    QDateTime syncedAt;
    // KCC profile used for a comic; empty for EPUB and PDF (SPEC-COMICS.md §6).
    QString conversionProfile;
    // "rtl" or "ltr" for a comic; empty for EPUB and PDF, and for comics synced before the
    // field existed, which count as "ltr" (SPEC-COMICS.md §6).
    QString readingDirection;
    // The user chose to keep this book on the Kobo although sync would remove it
    // (SPEC-UNSYNC.md §4); keptReason says why ("deselected" or "not in Komga").
    bool kept = false;
    QString keptReason;
    // Absolute path on the computer of a local file (SPEC-LOCAL.md §3); empty for Komga books.
    QString localPath;
    // The book's series and number, so that the Kobo's library can get them without Komga
    // once the Kobo has imported the book (SPEC-KOBO-BOOKS.md §3.2); empty when none.
    QString series;
    double seriesNumber = 0;

    bool operator==(const ManifestEntry &other) const = default;
};

// Record of the books Kobold placed on a device (SPEC.md §5).
class Manifest
{
public:
    explicit Manifest(const QString &deviceRoot);

    static QString relativeFilePath();
    QString filePath() const;

    // A missing file is an empty manifest. Returns false on an unreadable or invalid file;
    // in that case save() refuses to overwrite it.
    bool load();
    bool save();

    QString komgaUrl() const;
    void setKomgaUrl(const QString &url);

    const QList<ManifestEntry> &entries() const;
    const ManifestEntry *findByBookId(const QString &bookId) const;

    // Inserts or replaces the entry with the same book id.
    void upsert(const ManifestEntry &entry);
    bool remove(const QString &bookId);

    QString errorString() const;
    // load() found an empty file: a write was interrupted (Kobo unplugged). The manifest is then
    // empty, and the sync takes back the files of its books (SyncPlanInput::adoptExisting).
    bool wasEmpty() const
    {
        return m_wasEmpty;
    }

private:
    enum class State {
        NotLoaded,
        Loaded,
        Invalid,
    };

    QString m_deviceRoot;
    QString m_komgaUrl;
    QList<ManifestEntry> m_entries;
    State m_state = State::NotLoaded;
    QString m_errorString;
    bool m_wasEmpty = false;
};

// Compares two Komga base URLs, ignoring a trailing slash and the case of scheme and host.
bool isSameKomgaUrl(const QString &a, const QString &b);

} // namespace Kobold
