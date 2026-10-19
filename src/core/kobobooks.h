// SPDX-License-Identifier: MIT

#pragma once

#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QList>
#include <QString>
#include <QStringList>

class QSqlDatabase;

namespace Kobold
{

// A book the Kobo knows (SPEC-KOBO-BOOKS.md §2).
struct KoboBook {
    enum class Origin {
        Store,
        Komga,
        Kobold,
        Other,
    };

    // Short id for the command line: the first 8 hex digits of the SHA-1 of contentId.
    QString id;
    QString contentId;
    QString title;
    QString authors;
    QString series;
    double seriesNumber = -1;
    Origin origin = Origin::Other;
    bool downloaded = false;
    // 0 unread, 1 reading, 2 finished.
    int readStatus = 0;
    int percentRead = 0;
    QDateTime lastRead;
    bool hidden = false;
    // Files only: path relative to the device root (the original path when hidden).
    QString path;
    // The Kobo's own thumbnail of the book, absolute; empty when it has none.
    QString thumbnail;

    bool isFile() const
    {
        return !path.isEmpty();
    }
};

// Leaves out the books of adult series while parental control is locked (SPEC-PARENTAL.md).
// The Kobo knows no genres or ages: a book is matched by its series, or for a file by its
// title and path, against the adult series titles.
struct KoboBooksAdultFilter {
    bool active = false;
    // The adult series could not be read: every Komga and Kobold book is left out.
    bool unknown = false;
    // Adult series titles, folded (foldForComparison).
    QSet<QString> titles;

    bool hides(const KoboBook &book) const;
};

// One recorded change (SPEC-KOBO-BOOKS.md §4).
struct KoboBookHistoryEntry {
    QString batch;
    QDateTime date;
    QString contentId;
    QString title;
    // "visibility", "file", "readStatus", "series" ("<name>\x1f<number>\x1f<SeriesID>") or
    // "deleted" ("present:<path on the Kobo>" and "deleted:<backup holding the rows and the file>").
    QString field;
    QString before;
    QString after;
    // For an undo: the batch it reverts.
    QString undoes;
};

struct KoboBackup {
    QString path;
    QDateTime date;
};

// Lists the books of a Kobo and changes their visibility or read status, always after a
// backup, with a history that can be undone (SPEC-KOBO-BOOKS.md). Only for a mounted Kobo that
// nothing else is using.
class KoboBooks
{
public:
    enum class Change {
        Hide,
        Show,
        MarkRead,
        MarkUnread,
        // Not for store books, which the store would bring back (SPEC-KOBO-BOOKS.md §3.1).
        Delete,
    };

    struct ListResult {
        bool ok = false;
        QString errorString;
        QList<KoboBook> books;
    };

    struct ApplyResult {
        bool ok = false;
        QString errorString;
        int changed = 0;
        // setSeries: books with a series the Kobo has not imported yet (no row in its library).
        int pending = 0;
        QString batch;
        QString backup;
    };

    KoboBooks(const QString &deviceRoot, const QString &serial, const QString &backupsRoot = defaultBackupsRoot());

    // ~/.local/share/kobold/backups
    static QString defaultBackupsRoot();

    ListResult list() const;
    // Backs up, then applies every change in one transaction; on error puts the backup back.
    ApplyResult apply(const QList<std::pair<QString, Change>> &changes);
    // A series to give a sideloaded book (SPEC-KOBO-BOOKS.md §3.2).
    struct Series {
        QString name;
        double number = 0;
    };
    // Sets the series of sideloaded books the Kobo already imported (path relative to the
    // root → series), so that it groups them: the Kobo groups by content.SeriesID, which it
    // leaves empty for sideloaded books. SeriesID gets the series name, as calibre's Kobo driver
    // does. Books not imported yet, or already right, are left alone; a backup comes first.
    ApplyResult setSeries(const QHash<QString, Series> &series);
    // The series Kobold's manifest on this device keeps for its books, by path; read offline.
    static QHash<QString, Series> manifestSeries(const QString &deviceRoot);
    // Reverts a batch from the history (the latest not undone yet when empty), after a backup.
    ApplyResult undo(const QString &batch = QString());
    QList<KoboBookHistoryEntry> history() const;
    // The batch undo() reverts by default; empty when there is none.
    QString lastUndoableBatch() const;
    bool isUndone(const QString &batch) const;

    // Copies the database (and its -wal/-shm files) and checks the copy. Returns the folder.
    QString backup(QString *errorString) const;
    QList<KoboBackup> backups() const;
    // Backs up the current state, then puts that backup back.
    ApplyResult restore(const QString &backupPath);
    // Keeps backups younger than 90 days, and always the five most recent.
    void prune() const;

    // Files hidden on this device: original path -> hidden path, both relative to the root.
    // The sync treats them as present, and removes the hidden copy with the book.
    static QHash<QString, QString> hiddenFiles(const QString &deviceRoot);

    // Where the Kobo keeps the thumbnail of an image id (content.ImageId):
    // .kobo-images/<h & 0xff>/<(h >> 8) & 0xff>/<id> - N3_LIBRARY_GRID.parsed, h being Qt 3's
    // qHash of the id, as calibre's Kobo driver writes them. Empty when none exists.
    static QString thumbnailPath(const QString &deviceRoot, const QString &imageId);

private:
    QString databasePath() const;
    QString serialFolder() const;
    QString historyPath() const;
    QString hiddenIndexPath() const;
    bool appendHistory(const QList<KoboBookHistoryEntry> &entries, QString *errorString) const;
    // Deletes the book's rows; moves its file into the backup folder.
    bool deleteBook(QSqlDatabase &database, const QString &contentId, QString *errorString);
    // Applies before/after values; reverse swaps them (undo).
    ApplyResult write(const QList<KoboBookHistoryEntry> &entries, const QString &batch, bool reverse);

    QString m_deviceRoot;
    QString m_serial;
    QString m_backupsRoot;
};

} // namespace Kobold
