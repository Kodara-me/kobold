// SPDX-License-Identifier: MIT

#include "syncplanner.h"
#include "filenaming.h"
#include "localfiles.h"
#include "shelflibrary.h"

#include <KLocalizedString>

namespace Kobold
{

namespace
{
QString foldCase(const QString &path)
{
    return path.toCaseFolded();
}

class PathRegistry
{
public:
    void add(const QString &path)
    {
        m_paths.insert(foldCase(path));
    }

    void remove(const QString &path)
    {
        m_paths.remove(foldCase(path));
    }

    bool contains(const QString &path) const
    {
        return m_paths.contains(foldCase(path));
    }

private:
    QSet<QString> m_paths;
};

QString joinPath(const QString &folder, const QString &fileName)
{
    return folder.isEmpty() ? fileName : folder + QLatin1Char('/') + fileName;
}

// Returns the first candidate path that is free or already owned, or an empty string.
QString choosePath(const KomgaBook &book, const QString &folder, const PathRegistry &occupied, const QString &ownPath)
{
    const QStringList candidates = FileNaming::candidateFileNames(book);
    for (const QString &fileName : candidates) {
        const QString path = joinPath(folder, fileName);
        if (!ownPath.isEmpty() && foldCase(path) == foldCase(ownPath)) {
            return ownPath;
        }
        if (!occupied.contains(path)) {
            return path;
        }
    }
    return {};
}

std::optional<QString> skipReason(const KomgaBook &book, const SyncPlanInput &input)
{
    if (input.epubSyncedByKomga && book.mediaType == MediaTypeEpub && !LocalFiles::isLocalId(book.id)) {
        return i18n("synced by Komga");
    }
    if (!isSupportedMediaType(book.mediaType)) {
        return i18n("unsupported format (%1)", book.mediaType.isEmpty() ? i18nc("@item unknown media type", "unknown") : book.mediaType);
    }
    if (book.mediaStatus != MediaStatusReady) {
        return i18n("not ready in Komga (status %1)", book.mediaStatus.isEmpty() ? i18nc("@item unknown media status", "unknown") : book.mediaStatus);
    }
    if (isComicMediaType(book.mediaType) && input.comicProfile.isEmpty()) {
        return input.comicUnavailableReason.isEmpty() ? i18n("comics cannot be converted for this device") : input.comicUnavailableReason;
    }
    return std::nullopt;
}

SyncAction makeSkip(const QString &bookId, const QString &reason, const std::optional<KomgaBook> &book = std::nullopt)
{
    SyncAction action;
    action.type = SyncAction::Type::Skip;
    action.bookId = bookId;
    action.reason = reason;
    action.book = book;
    return action;
}
}

bool isKomgaFileChanged(const ManifestEntry &entry, const KomgaBook &book)
{
    if (entry.sourceMediaType != book.mediaType) {
        return true;
    }
    if (!entry.komgaFileHash.isEmpty() && !book.fileHash.isEmpty()) {
        return entry.komgaFileHash != book.fileHash;
    }
    return entry.komgaFileLastModified != book.fileLastModified || entry.komgaSizeBytes != book.sizeBytes;
}

QList<SyncAction> planSync(const SyncPlanInput &input)
{
    QString folder = input.booksFolder;
    while (folder.startsWith(QLatin1Char('/'))) {
        folder.remove(0, 1);
    }
    while (folder.endsWith(QLatin1Char('/'))) {
        folder.chop(1);
    }

    QString localFolder = input.localFolder;
    while (localFolder.endsWith(QLatin1Char('/'))) {
        localFolder.chop(1);
    }
    const auto folderFor = [&folder, &localFolder](const KomgaBook &book) {
        return LocalFiles::isLocalId(book.id) ? localFolder : folder;
    };

    const QSet<QString> selected(input.selection.cbegin(), input.selection.cend());

    QHash<QString, ManifestEntry> managed;
    for (const ManifestEntry &entry : input.manifestEntries) {
        managed.insert(entry.komgaBookId, entry);
    }

    PathRegistry existing;
    for (const QString &path : input.existingPaths) {
        existing.add(path);
    }

    PathRegistry occupied = existing;
    for (const ManifestEntry &entry : input.manifestEntries) {
        occupied.add(entry.path);
    }

    QList<SyncAction> removals;
    QSet<QString> takenByKomga;
    QSet<QString> adopted;
    for (const ManifestEntry &entry : input.manifestEntries) {
        const bool toKomga = input.epubSyncedByKomga && entry.sourceMediaType == MediaTypeEpub && !LocalFiles::isLocalId(entry.komgaBookId);
        const bool isSelected = selected.contains(entry.komgaBookId);
        const bool missing = input.missingFromKomga.contains(entry.komgaBookId);
        const bool forced = input.removeNow.contains(entry.komgaBookId);
        // Kept books stay until selected again or removed on request (SPEC-UNSYNC.md §4).
        if (entry.kept && !forced) {
            continue;
        }
        if (isSelected && !toKomga && !missing && !forced) {
            continue;
        }
        SyncAction action;
        action.type = SyncAction::Type::Remove;
        action.bookId = entry.komgaBookId;
        action.currentPath = entry.path;
        action.missingFromKomga = missing;
        action.stillSelected = isSelected;
        if (missing) {
            action.reason = ShelfLibrary::isShelfId(entry.komgaBookId) ? i18n("not on its shelf") : i18n("not in Komga");
            takenByKomga.insert(entry.komgaBookId);
        } else if (toKomga) {
            action.reason = i18n("now synced by Komga");
            takenByKomga.insert(entry.komgaBookId);
        }
        removals.append(action);
        // Removals run first, so their paths are free for this run.
        occupied.remove(entry.path);
    }

    QHash<QString, SyncAction> actions;

    // Managed books first, so they keep their names when new books collide with them.
    for (const QString &bookId : input.selection) {
        const auto entryIt = managed.constFind(bookId);
        const auto bookIt = input.books.constFind(bookId);
        if (entryIt == managed.cend() || bookIt == input.books.cend() || skipReason(*bookIt, input)) {
            continue;
        }
        const ManifestEntry &entry = *entryIt;
        const KomgaBook &book = *bookIt;
        if (input.removeNow.contains(bookId)) {
            continue;
        }

        const QString targetPath = choosePath(book, folderFor(book), occupied, entry.path);
        if (targetPath.isEmpty()) {
            actions.insert(bookId, makeSkip(bookId, i18n("no free file name on the device"), book));
            continue;
        }
        occupied.add(targetPath);

        SyncAction action;
        action.type = SyncAction::Type::Update;
        action.bookId = bookId;
        action.targetPath = targetPath;
        action.currentPath = entry.path;
        action.book = book;
        const QString expectedProfile = isComicMediaType(book.mediaType) ? input.comicProfile : QString();
        if (isKomgaFileChanged(entry, book)) {
            action.needsTransfer = true;
            action.reason = i18n("file changed in Komga");
        } else if (entry.conversionProfile != expectedProfile) {
            action.needsTransfer = true;
            action.reason = i18n("profile changed");
        } else if (isComicMediaType(book.mediaType)
                   && (entry.readingDirection.isEmpty() ? QString(DirectionLeftToRight) : entry.readingDirection)
                       != input.comicDirections.value(bookId, DirectionLeftToRight)) {
            action.needsTransfer = true;
            action.reason = i18n("reading direction changed");
        } else if (!existing.contains(entry.path)) {
            action.needsTransfer = true;
            action.reason = i18n("file missing on the device");
        } else if (targetPath != entry.path) {
            action.reason = i18n("file name changed");
        } else if (entry.kept) {
            action.reason = i18n("selected again");
        } else {
            continue;
        }
        action.clearsKept = entry.kept;
        actions.insert(bookId, action);
    }

    for (const QString &bookId : input.selection) {
        if (actions.contains(bookId) || takenByKomga.contains(bookId)) {
            continue; // Already planned, or reported by its removal.
        }
        const auto unavailableIt = input.unavailable.constFind(bookId);
        if (unavailableIt != input.unavailable.cend()) {
            actions.insert(bookId, makeSkip(bookId, *unavailableIt));
            continue;
        }
        const auto bookIt = input.books.constFind(bookId);
        if (bookIt == input.books.cend()) {
            actions.insert(bookId, makeSkip(bookId, i18n("metadata not available")));
            continue;
        }
        if (const auto reason = skipReason(*bookIt, input)) {
            actions.insert(bookId, makeSkip(bookId, *reason, *bookIt));
            continue;
        }
        if (managed.contains(bookId)) {
            continue; // Up to date.
        }

        if (input.adoptExisting) {
            const QString first = joinPath(folderFor(*bookIt), FileNaming::candidateFileNames(*bookIt).value(0));
            if (existing.contains(first) && !adopted.contains(foldCase(first))) {
                adopted.insert(foldCase(first));
                SyncAction action;
                action.type = SyncAction::Type::Update;
                action.bookId = bookId;
                action.currentPath = first;
                action.targetPath = first;
                action.needsTransfer = true;
                action.book = *bookIt;
                actions.insert(bookId, action);
                continue;
            }
        }

        const QString targetPath = choosePath(*bookIt, folderFor(*bookIt), occupied, QString());
        if (targetPath.isEmpty()) {
            actions.insert(bookId, makeSkip(bookId, i18n("no free file name on the device"), *bookIt));
            continue;
        }
        occupied.add(targetPath);

        SyncAction action;
        action.type = SyncAction::Type::Add;
        action.bookId = bookId;
        action.targetPath = targetPath;
        action.needsTransfer = true;
        action.book = *bookIt;
        actions.insert(bookId, action);
    }

    QList<SyncAction> plan = removals;
    for (const QString &bookId : input.selection) {
        const auto it = actions.constFind(bookId);
        if (it != actions.cend()) {
            plan.append(*it);
        }
    }
    return plan;
}

} // namespace Kobold
