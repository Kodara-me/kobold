// SPDX-License-Identifier: MIT

#pragma once

#include "komgabook.h"
#include "manifest.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace Kobold
{

struct SyncAction {
    enum class Type {
        Add,
        Update,
        Remove,
        Skip,
        // Leave the file and mark it kept (SPEC-UNSYNC.md §4); only made from a Remove the
        // user answered with Keep.
        Keep,
    };

    Type type = Type::Skip;
    QString bookId;
    // Add, Update: path to write, relative to the device root.
    QString targetPath;
    // Update, Remove: path recorded in the manifest.
    QString currentPath;
    // Update: false when only the file name changes.
    bool needsTransfer = false;
    // Update, Skip, and Remove of a book now synced by Komga: human-readable cause.
    QString reason;
    // Add, Update, and Skip when metadata is known.
    std::optional<KomgaBook> book;
    // Remove: Komga no longer has the book (SPEC-UNSYNC.md case 3); stillSelected when the
    // selection still asks for it.
    bool missingFromKomga = false;
    bool stillSelected = false;
    // Update of a kept book selected again: the kept mark goes away (SPEC-UNSYNC.md §4).
    bool clearsKept = false;
};

struct SyncPlanInput {
    // Selected Komga book ids.
    QStringList selection;
    // Metadata of selected books that Komga returned.
    QHash<QString, KomgaBook> books;
    // Selected books Komga could not return, with the reason (e.g. "not found in Komga").
    QHash<QString, QString> unavailable;
    QList<ManifestEntry> manifestEntries;
    // Relative paths of files present on the device: every file in the books folder and
    // every manifest path that exists. Compared case-insensitively (FAT file system).
    QSet<QString> existingPaths;
    // Folder for books, relative to the device root (e.g. "kobold").
    QString booksFolder;
    // KCC profile for comics on this device; empty when comics cannot be converted, in which
    // case they are skipped with comicUnavailableReason (SPEC-COMICS.md §6).
    QString comicProfile;
    QString comicUnavailableReason;
    // Reading direction of selected comics by book id: "rtl" or "ltr" (missing means "ltr").
    QHash<QString, QString> comicDirections;
    // The Kobo gets its EPUB books from Komga's own Kobo sync (SPEC-KOBO-SYNC.md §3): EPUB
    // books are skipped, and those Kobold placed earlier are removed.
    bool epubSyncedByKomga = false;
    // Books Komga answered "not found" for, selected or not (SPEC-UNSYNC.md case 3).
    QSet<QString> missingFromKomga;
    // Folder for local files, relative to the device root (SPEC-LOCAL.md §3).
    QString localFolder = QStringLiteral("local");
    // The manifest was lost (an empty file): a selected book whose file already sits at its
    // first name is taken back and copied again over it, instead of getting a second copy.
    bool adoptExisting = false;
    // Books to remove now although kept ("Remove Now", SPEC-UNSYNC.md §4).
    QSet<QString> removeNow;
};

inline constexpr QLatin1StringView DirectionLeftToRight("ltr");
inline constexpr QLatin1StringView DirectionRightToLeft("rtl");

// True when the Komga file differs from the one recorded in the manifest (SPEC.md §8).
bool isKomgaFileChanged(const ManifestEntry &entry, const KomgaBook &book);

// Computes the sync plan (SPEC.md §8). Pure function: no I/O.
// Order: Remove actions first, then Add / Update / Skip in selection order.
// Books that need no action are omitted.
QList<SyncAction> planSync(const SyncPlanInput &input);

} // namespace Kobold
