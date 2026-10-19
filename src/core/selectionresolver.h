// SPDX-License-Identifier: MIT

#pragma once

#include "komgabook.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace Kobold
{

class KomgaClient;
class SelectionStore;

// Books of one followed series, collection or read list at resolution time.
struct ResolvedGroup {
    enum class Kind {
        Series,
        Collection,
        ReadList,
    };

    Kind kind = Kind::Series;
    QString id;
    QString name;
    // Series only: normalized volume range, empty for the whole series.
    QString volumes;
    QList<KomgaBook> books;

    // "series Naruto", "collection Best", "read list Kobo".
    QString description() const;
};

// Effective selection (SPEC-SELECTION.md §2, §7).
struct ResolvedSelection {
    enum class Status {
        Ok,
        // A followed group could not be read: exit code 4.
        KomgaError,
        // A followed group no longer exists in Komga: exit code 2.
        GroupMissing,
    };

    Status status = Status::Ok;
    QString errorString;
    QStringList directBookIds;
    QList<ResolvedGroup> groups;
    // Direct books, then books of each group, without duplicates.
    QStringList bookIds;

    bool ok() const
    {
        return status == Status::Ok;
    }
    bool contains(const QString &bookId) const;
    // The first group selecting bookId, or nullptr.
    const ResolvedGroup *groupOf(const QString &bookId) const;
};

// Reads every followed group from Komga, one after the other. Without followed groups the
// callback runs immediately with the direct books.
void resolveSelection(KomgaClient *client, const SelectionStore &selection, std::function<void(const ResolvedSelection &)> callback);

} // namespace Kobold
