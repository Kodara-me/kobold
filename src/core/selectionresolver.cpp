// SPDX-License-Identifier: MIT

#include "selectionresolver.h"
#include "komgaclient.h"
#include "selectionstore.h"
#include "volumerange.h"

#include <KLocalizedString>

#include <QSet>

#include <memory>

namespace Kobold
{

namespace
{
struct PendingGroup {
    ResolvedGroup::Kind kind;
    QString id;
    QString volumes;
};

// A followed group named with its kind, as one translatable phrase per kind.
QString groupLabel(ResolvedGroup::Kind kind, const QString &name)
{
    switch (kind) {
    case ResolvedGroup::Kind::Series:
        return i18nc("@item followed group", "series %1", name);
    case ResolvedGroup::Kind::Collection:
        return i18nc("@item followed group", "collection %1", name);
    case ResolvedGroup::Kind::ReadList:
        return i18nc("@item followed group", "read list %1", name);
    }
    return name;
}

QString removeOption(ResolvedGroup::Kind kind)
{
    switch (kind) {
    case ResolvedGroup::Kind::Series:
        return QStringLiteral("--series");
    case ResolvedGroup::Kind::Collection:
        return QStringLiteral("--collection");
    case ResolvedGroup::Kind::ReadList:
        return QStringLiteral("--readlist");
    }
    return {};
}

class Resolver : public std::enable_shared_from_this<Resolver>
{
public:
    Resolver(KomgaClient *client, std::function<void(const ResolvedSelection &)> callback)
        : m_client(client)
        , m_callback(std::move(callback))
    {
    }

    void start(const SelectionStore &selection)
    {
        m_result.directBookIds = selection.bookIds();
        for (const FollowedSeries &series : selection.followedSeries()) {
            m_pending.append({ResolvedGroup::Kind::Series, series.id, series.volumes});
        }
        for (const QString &id : selection.followedCollections()) {
            m_pending.append({ResolvedGroup::Kind::Collection, id, QString()});
        }
        for (const QString &id : selection.followedReadLists()) {
            m_pending.append({ResolvedGroup::Kind::ReadList, id, QString()});
        }
        next();
    }

private:
    void next()
    {
        if (m_index >= m_pending.size()) {
            finish();
            return;
        }
        const PendingGroup group = m_pending.at(m_index++);
        switch (group.kind) {
        case ResolvedGroup::Kind::Series:
            resolveSeries(group);
            break;
        case ResolvedGroup::Kind::Collection:
            resolveCollection(group);
            break;
        case ResolvedGroup::Kind::ReadList:
            resolveReadList(group);
            break;
        }
    }

    // Returns true (and finishes) when error is set.
    bool failed(const PendingGroup &group, const KomgaError &error)
    {
        if (!error.isError()) {
            return false;
        }
        if (error.kind == KomgaError::Kind::NotFound) {
            m_result.status = ResolvedSelection::Status::GroupMissing;
            m_result.errorString = i18n("followed %1 no longer exists in Komga; run: kobold select remove %2 %3",
                                       groupLabel(group.kind, group.id),
                                       removeOption(group.kind),
                                       group.id);
        } else {
            m_result.status = ResolvedSelection::Status::KomgaError;
            m_result.errorString = i18n("cannot read followed %1: %2", groupLabel(group.kind, group.id), error.message);
        }
        finish();
        return true;
    }

    void resolveSeries(const PendingGroup &group)
    {
        auto self = shared_from_this();
        m_client->fetchOneSeries(group.id, [self, group](const KomgaResult<KomgaSeries> &series) {
            if (self->failed(group, series.error)) {
                return;
            }
            self->m_client->fetchSeriesBooks(group.id, [self, group, name = series.value.title](const KomgaResult<QList<KomgaBook>> &books) {
                if (self->failed(group, books.error)) {
                    return;
                }
                ResolvedGroup resolved{group.kind, group.id, name, group.volumes, {}};
                const auto range = group.volumes.isEmpty() ? std::nullopt : VolumeRange::parse(group.volumes);
                for (const KomgaBook &book : books.value) {
                    if (!range || range->contains(book.numberSort)) {
                        resolved.books.append(book);
                    }
                }
                self->m_result.groups.append(resolved);
                self->next();
            });
        });
    }

    void resolveCollection(const PendingGroup &group)
    {
        auto self = shared_from_this();
        m_client->fetchOneCollection(group.id, [self, group](const KomgaResult<KomgaCollection> &collection) {
            if (self->failed(group, collection.error)) {
                return;
            }
            self->m_client->fetchCollectionSeries(group.id,
                                                  QString(),
                                                  [self, group, name = collection.value.name](const KomgaResult<QList<KomgaSeries>> &series) {
                                                      if (self->failed(group, series.error)) {
                                                          return;
                                                      }
                                                      auto resolved = std::make_shared<ResolvedGroup>(ResolvedGroup{group.kind, group.id, name, QString(), {}});
                                                      self->collectSeriesBooks(group, series.value, 0, resolved);
                                                  });
        });
    }

    void collectSeriesBooks(const PendingGroup &group, const QList<KomgaSeries> &series, qsizetype index, std::shared_ptr<ResolvedGroup> resolved)
    {
        if (index >= series.size()) {
            m_result.groups.append(*resolved);
            next();
            return;
        }
        auto self = shared_from_this();
        m_client->fetchSeriesBooks(series.at(index).id, [self, group, series, index, resolved](const KomgaResult<QList<KomgaBook>> &books) {
            if (self->failed(group, books.error)) {
                return;
            }
            resolved->books.append(books.value);
            self->collectSeriesBooks(group, series, index + 1, resolved);
        });
    }

    void resolveReadList(const PendingGroup &group)
    {
        auto self = shared_from_this();
        m_client->fetchOneReadList(group.id, [self, group](const KomgaResult<KomgaReadList> &readList) {
            if (self->failed(group, readList.error)) {
                return;
            }
            self->m_client->fetchReadListBooks(group.id, QString(), [self, group, name = readList.value.name](const KomgaResult<QList<KomgaBook>> &books) {
                if (self->failed(group, books.error)) {
                    return;
                }
                self->m_result.groups.append(ResolvedGroup{group.kind, group.id, name, QString(), books.value});
                self->next();
            });
        });
    }

    void finish()
    {
        if (m_result.ok()) {
            QSet<QString> seen;
            const auto add = [this, &seen](const QString &id) {
                if (!seen.contains(id)) {
                    seen.insert(id);
                    m_result.bookIds.append(id);
                }
            };
            for (const QString &id : std::as_const(m_result.directBookIds)) {
                add(id);
            }
            for (const ResolvedGroup &group : std::as_const(m_result.groups)) {
                for (const KomgaBook &book : group.books) {
                    add(book.id);
                }
            }
        }
        m_callback(m_result);
    }

    KomgaClient *m_client;
    std::function<void(const ResolvedSelection &)> m_callback;
    QList<PendingGroup> m_pending;
    qsizetype m_index = 0;
    ResolvedSelection m_result;
};
}

QString ResolvedGroup::description() const
{
    return groupLabel(kind, name.isEmpty() ? id : name);
}

bool ResolvedSelection::contains(const QString &bookId) const
{
    return bookIds.contains(bookId);
}

const ResolvedGroup *ResolvedSelection::groupOf(const QString &bookId) const
{
    for (const ResolvedGroup &group : groups) {
        for (const KomgaBook &book : group.books) {
            if (book.id == bookId) {
                return &group;
            }
        }
    }
    return nullptr;
}

void resolveSelection(KomgaClient *client, const SelectionStore &selection, std::function<void(const ResolvedSelection &)> callback)
{
    auto resolver = std::make_shared<Resolver>(client, std::move(callback));
    resolver->start(selection);
}

} // namespace Kobold
