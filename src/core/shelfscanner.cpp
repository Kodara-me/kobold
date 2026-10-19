// SPDX-License-Identifier: MIT

#include "shelfscanner.h"
#include "localfiles.h"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QtConcurrentRun>

namespace Kobold
{

namespace
{
constexpr qint64 FingerprintBytes = 1024 * 1024;

struct Known {
    qint64 size = 0;
    QDateTime modified;
    std::optional<double> number;
};

struct FoundFile {
    QString path;
    qint64 size = 0;
    QDateTime modified;
    // Read this time: fingerprint, media type and metadata are set.
    bool read = false;
    QString fingerprint;
    QString mediaType;
    BookMetadata metadata;
};

struct WorkResult {
    bool reachable = false;
    bool cancelled = false;
    QList<FoundFile> files;
};

// Runs on a worker thread: no database access.
WorkResult walk(const QString &root,
                const QHash<QString, Known> &known,
                const std::shared_ptr<std::atomic_bool> &cancelled,
                const std::function<void(int, int)> &progress)
{
    WorkResult result;
    if (!QFileInfo(root).isDir()) {
        return result;
    }
    result.reachable = true;
    const QDir rootDir(root);
    QDirIterator iterator(root, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (iterator.hasNext()) {
        const QFileInfo info = iterator.nextFileInfo();
        if (info.fileName().startsWith(QLatin1Char('.')) || LocalFiles::mediaTypeFor(info.fileName()).isEmpty()) {
            continue;
        }
        FoundFile file;
        file.path = rootDir.relativeFilePath(info.absoluteFilePath());
        file.size = info.size();
        file.modified = info.lastModified().toUTC();
        result.files.append(file);
    }

    int done = 0;
    const int total = int(result.files.size());
    // Folders where some book has a volume number: the others are one-shots (§4.2).
    QSet<QString> numberedFolders;
    QHash<QString, bool> embeddedSeries;
    for (FoundFile &file : result.files) {
        if (cancelled->load()) {
            result.cancelled = true;
            return result;
        }
        const auto it = known.constFind(file.path);
        const bool unchanged = it != known.cend() && it->size == file.size && it->modified == file.modified;
        if (unchanged) {
            if (it->number) {
                numberedFolders.insert(QFileInfo(file.path).path());
            }
        } else {
            const QString absolute = rootDir.filePath(file.path);
            file.read = true;
            file.fingerprint = ShelfScanner::fingerprint(absolute);
            file.mediaType = LocalFiles::mediaTypeFor(absolute);
            const BookMetadata embedded = ShelfMetadata::readEmbedded(absolute);
            file.metadata = ShelfMetadata::merge(embedded, ShelfMetadata::fromNames(file.path));
            embeddedSeries.insert(file.path, !embedded.series.isEmpty());
            if (file.metadata.number) {
                numberedFolders.insert(QFileInfo(file.path).path());
            }
        }
        progress(++done, total);
    }

    // A folder whose books have no volume numbers is not a series (§4.2).
    for (FoundFile &file : result.files) {
        const QString folder = QFileInfo(file.path).path();
        if (file.read && !embeddedSeries.value(file.path) && folder != QLatin1String(".") && !numberedFolders.contains(folder)) {
            file.metadata.series.clear();
        }
    }
    return result;
}
}

ShelfScanner::ShelfScanner(ShelfDatabase *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_cancelled(std::make_shared<std::atomic_bool>(false))
{
}

ShelfScanner::~ShelfScanner()
{
    m_cancelled->store(true);
}

QString ShelfScanner::fingerprint(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray head = file.read(FingerprintBytes);
    return QString::number(file.size()) + QLatin1Char('-') + QString::fromLatin1(QCryptographicHash::hash(head, QCryptographicHash::Sha1).toHex());
}

void ShelfScanner::cancel()
{
    m_cancelled->store(true);
}

void ShelfScanner::scan(qint64 shelfId, int forgetAfterDays)
{
    m_cancelled->store(false);
    const std::optional<Shelf> shelf = m_database->shelf(shelfId);
    if (!shelf) {
        ShelfScanReport report;
        report.errorString = i18n("unknown shelf");
        QMetaObject::invokeMethod(
            this,
            [this, report]() {
                Q_EMIT finished(report);
            },
            Qt::QueuedConnection);
        return;
    }

    const QList<ShelfBook> books = m_database->books(shelfId);
    QHash<QString, Known> known;
    for (const ShelfBook &book : books) {
        known.insert(book.path, Known{book.size, book.modified, book.metadata.number});
    }

    QPointer<ShelfScanner> self(this);
    const auto progress = [self](int done, int total) {
        QMetaObject::invokeMethod(
            self.data(),
            [self, done, total]() {
                if (self) {
                    Q_EMIT self->progress(done, total);
                }
            },
            Qt::QueuedConnection);
    };

    auto *watcher = new QFutureWatcher<WorkResult>(this);
    connect(watcher, &QFutureWatcher<WorkResult>::finished, this, [this, watcher, shelfId, books, forgetAfterDays]() {
        watcher->deleteLater();
        const WorkResult work = watcher->result();
        ShelfScanReport report;
        if (work.cancelled || m_cancelled->load()) {
            report.cancelled = true;
            Q_EMIT finished(report);
            return;
        }
        if (!work.reachable) {
            report.unavailable = true;
            report.errorString = i18n("the folder of this shelf cannot be read; is the share mounted?");
            Q_EMIT finished(report);
            return;
        }

        const QDateTime now = QDateTime::currentDateTimeUtc();
        QHash<QString, ShelfBook> byPath;
        for (const ShelfBook &book : books) {
            byPath.insert(book.path, book);
        }
        QSet<QString> seen;
        for (const FoundFile &file : work.files) {
            seen.insert(file.path);
        }

        m_database->transaction();
        QSet<qint64> movedIds;
        for (const FoundFile &file : work.files) {
            auto existing = byPath.find(file.path);
            if (existing != byPath.end()) {
                ShelfBook book = *existing;
                const bool wasMissing = book.missingSince.isValid();
                book.missingSince = QDateTime();
                if (file.read) {
                    book.size = file.size;
                    book.modified = file.modified;
                    book.fingerprint = file.fingerprint;
                    book.mediaType = file.mediaType;
                    book.metadata = file.metadata;
                    m_database->saveBook(book);
                    ++report.updated;
                } else {
                    if (wasMissing) {
                        m_database->saveBook(book);
                    }
                    ++report.unchanged;
                }
                continue;
            }
            // A new path: a moved book when a vanished one has the same fingerprint (§3).
            ShelfBook book;
            for (const ShelfBook &candidate : books) {
                if (!seen.contains(candidate.path) && !movedIds.contains(candidate.id) && !file.fingerprint.isEmpty()
                    && candidate.fingerprint == file.fingerprint) {
                    book = candidate;
                    movedIds.insert(candidate.id);
                    break;
                }
            }
            const bool moved = book.id != 0;
            book.shelfId = shelfId;
            book.path = file.path;
            book.size = file.size;
            book.modified = file.modified;
            book.fingerprint = file.fingerprint;
            book.mediaType = file.mediaType;
            book.metadata = file.metadata;
            book.missingSince = QDateTime();
            m_database->saveBook(book);
            moved ? ++report.moved : ++report.added;
        }

        for (ShelfBook book : books) {
            if (seen.contains(book.path) || movedIds.contains(book.id)) {
                continue;
            }
            if (!book.missingSince.isValid()) {
                book.missingSince = now;
                m_database->saveBook(book);
                ++report.missing;
            } else if (book.missingSince.daysTo(now) >= forgetAfterDays) {
                m_database->removeBook(book.id);
                ++report.forgotten;
            }
        }
        m_database->setLastScan(shelfId, now);
        m_database->commit();
        report.ok = true;
        Q_EMIT finished(report);
    });
    watcher->setFuture(QtConcurrent::run(walk, shelf->path, known, m_cancelled, progress));
}

} // namespace Kobold
