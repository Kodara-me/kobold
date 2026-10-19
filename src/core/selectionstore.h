// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace Kobold
{

// A series followed by the selection, optionally limited to a volume range
// (SPEC-SELECTION.md §2, §4).
struct FollowedSeries {
    QString id;
    // Normalized VolumeRange text; empty for the whole series.
    QString volumes;

    bool operator==(const FollowedSeries &other) const = default;
};

// Local selection: direct book ids and followed series, collections and read lists
// (SPEC.md §4, SPEC-SELECTION.md §5).
class SelectionStore
{
public:
    explicit SelectionStore(const QString &filePath);

    // ~/.local/share/kobold/selection.json: the former shared selection, now the starting
    // selection of a Kobo seen for the first time (SPEC-DEVICES.md §4).
    static QString defaultFilePath();
    // ~/.local/share/kobold/selections/<serial>.json. When it does not exist yet and the
    // shared file does, the shared file is copied there first.
    static QString koboFilePath(const QString &serial);

    QString filePath() const;

    // A missing file is an empty selection; version 1 files are accepted. Returns false on an
    // unreadable or invalid file; in that case save() refuses to overwrite it.
    bool load();
    // Always writes version 2.
    bool save();

    // Direct books, sorted, without duplicates.
    QStringList bookIds() const;
    bool contains(const QString &bookId) const;
    // Return true when the selection changed.
    bool add(const QString &bookId);
    bool remove(const QString &bookId);

    QList<FollowedSeries> followedSeries() const;
    // Follows the series, replacing the range of an existing entry. Returns true on change.
    bool followSeries(const QString &seriesId, const QString &volumes);
    bool unfollowSeries(const QString &seriesId);

    QStringList followedCollections() const;
    bool followCollection(const QString &collectionId);
    bool unfollowCollection(const QString &collectionId);

    QStringList followedReadLists() const;
    bool followReadList(const QString &readListId);
    bool unfollowReadList(const QString &readListId);

    // Files on the computer to put on the Kobo (SPEC-LOCAL.md §2): absolute paths, sorted,
    // without duplicates. Return true when the selection changed.
    QStringList localFiles() const;
    bool addLocalFile(const QString &absolutePath);
    bool removeLocalFile(const QString &absolutePath);

    // True when the selection follows at least one group.
    bool hasFollowedGroups() const;

    QString errorString() const;

private:
    enum class State {
        NotLoaded,
        Loaded,
        Invalid,
    };

    QString m_filePath;
    QStringList m_bookIds;
    QList<FollowedSeries> m_series;
    QStringList m_collections;
    QStringList m_readLists;
    QStringList m_localFiles;
    State m_state = State::NotLoaded;
    QString m_errorString;
};

} // namespace Kobold
