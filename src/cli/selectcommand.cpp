// SPDX-License-Identifier: MIT

#include "selectcommand.h"
#include "bookformat.h"
#include "console.h"
#include "exitcode.h"
#include "kobotarget.h"
#include "komgasession.h"
#include "parentalfilter.h"

#include "browsing.h"
#include "komgaclient.h"
#include "localfiles.h"
#include "selectionstore.h"
#include "shelflibrary.h"
#include "settings.h"
#include "syncexecutor.h"
#include "volumerange.h"

#include <KLocalizedString>

#include <QFileInfo>

namespace Kobold::Cli
{

namespace
{
constexpr int SearchResultLimit = 25;
}

SelectCommand::SelectCommand(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_apiKeyStore(apiKeyStore)
    , m_network(network)
{
}

SelectCommand::~SelectCommand() = default;

void SelectCommand::run(const QStringList &arguments, const Options &options)
{
    m_options = options;
    const QString action = arguments.value(0);

    // Search works without a designated Kobo; it then shows no selection markers.
    const KoboTarget target = resolveSelectionTarget(m_settings, options.kobo, options.devicePath);
    if (!target.ok && action != QLatin1String("search")) {
        fail(target.exitCode, target.errorString);
        return;
    }
    if (target.ok) {
        m_selection = std::make_unique<SelectionStore>(target.selectionPath());
        if (!m_selection->load()) {
            fail(UsageError, m_selection->errorString());
            return;
        }
        if (action != QLatin1String("search")) {
            printLine(i18n("Selection of %1:", target.kobo.name));
        }
    } else {
        printLine(i18n("Note: no selection markers (%1)", target.errorString));
        m_selection = std::make_unique<SelectionStore>(QString());
        m_selection->load();
    }

    const QString usage = i18n(
        "usage: kobold select add <id>... | add --series <series> [--volumes <range>] | add --collection <collection> | add --readlist <readlist>\n"
        "       kobold select remove <id>... | remove --series|--collection|--readlist <name or id>\n"
        "       kobold select add-file <path>... | remove-file <path>...\n"
        "       kobold select list | search <text> [--library <library>]");

    const QStringList rest = arguments.mid(1);
    const int groupOptions = int(!options.series.isEmpty()) + int(!options.collection.isEmpty()) + int(!options.readList.isEmpty());
    if (groupOptions > 1 || (options.hasVolumes && options.series.isEmpty())) {
        fail(UsageError, usage);
        return;
    }

    if (groupOptions == 1) {
        if (!rest.isEmpty() || (action != QLatin1String("add") && action != QLatin1String("remove"))) {
            fail(UsageError, usage);
            return;
        }
        const GroupKind kind = !options.series.isEmpty() ? GroupKind::Series : !options.collection.isEmpty() ? GroupKind::Collection : GroupKind::ReadList;
        const QString argument = !options.series.isEmpty() ? options.series : !options.collection.isEmpty() ? options.collection : options.readList;
        if (action == QLatin1String("remove")) {
            if (options.hasVolumes) {
                fail(UsageError, usage);
                return;
            }
            removeGroup(kind, argument);
            return;
        }
        QString volumes;
        if (options.hasVolumes) {
            const auto range = VolumeRange::parse(options.volumes);
            if (!range) {
                fail(UsageError, i18n("invalid volume range \"%1\"; examples: 1-10, 5, 1-3,7,10-", options.volumes));
                return;
            }
            volumes = range->toString();
        }
        addGroup(kind, argument, volumes);
        return;
    }

    if (action == QLatin1String("add-file") && !rest.isEmpty()) {
        addFiles(rest);
    } else if (action == QLatin1String("remove-file") && !rest.isEmpty()) {
        removeFiles(rest);
    } else if (action == QLatin1String("add") && !rest.isEmpty()) {
        add(rest);
    } else if (action == QLatin1String("remove") && !rest.isEmpty()) {
        remove(rest);
    } else if (action == QLatin1String("list") && rest.isEmpty()) {
        list();
    } else if (action == QLatin1String("search") && !rest.isEmpty()) {
        search(rest.join(QLatin1Char(' ')));
    } else {
        fail(UsageError, usage);
    }
}

void SelectCommand::add(const QStringList &bookIds)
{
    withKomga([this, bookIds]() {
        fetchBooks(bookIds, [this, bookIds](const QHash<QString, KomgaBook> &books, const QHash<QString, KomgaError> &errors) {
            // All or nothing: one unknown id leaves the selection unchanged.
            if (!errors.isEmpty()) {
                int exitCode = UsageError;
                for (auto it = errors.cbegin(); it != errors.cend(); ++it) {
                    if (it->kind == KomgaError::Kind::NotFound) {
                        printError(i18n("unknown book id %1", it.key()));
                    } else {
                        printError(QStringLiteral("%1: %2").arg(it.key(), it->message));
                        exitCode = KomgaFailure;
                    }
                }
                fail(exitCode, i18n("the selection was not changed"));
                return;
            }

            for (const QString &bookId : bookIds) {
                const KomgaBook &book = books.value(bookId);
                if (!m_selection->add(bookId)) {
                    printLine(i18n("Already selected: %1", bookLabel(book)));
                    continue;
                }
                printLine(i18n("Added: %1 (%2)", bookLabel(book), formatName(book)));
                if (!isSupportedMediaType(book.mediaType)) {
                    printWarning(i18n("%1 is not supported yet and will be skipped by sync", formatName(book)));
                }
            }
            if (!m_selection->save()) {
                fail(UsageError, m_selection->errorString());
                return;
            }
            Q_EMIT finished(Success);
        });
    });
}

void SelectCommand::remove(const QStringList &bookIds)
{
    QStringList notDirect;
    int removed = 0;
    for (const QString &bookId : bookIds) {
        if (m_selection->remove(bookId)) {
            ++removed;
        } else {
            notDirect.append(bookId);
        }
    }
    if (removed > 0 && !m_selection->save()) {
        fail(UsageError, m_selection->errorString());
        return;
    }
    printLine(i18np("Removed 1 book from the selection.", "Removed %1 books from the selection.", removed));

    if (notDirect.isEmpty() || !m_selection->hasFollowedGroups()) {
        for (const QString &bookId : std::as_const(notDirect)) {
            printWarning(i18n("%1 is not in the selection", bookId));
        }
        Q_EMIT finished(Success);
        return;
    }

    // Books selected through a group stay selected: say which group (SPEC-SELECTION.md §3).
    withKomga([this, notDirect]() {
        withResolvedSelection([this, notDirect](const ResolvedSelection &resolved) {
            for (const QString &bookId : notDirect) {
                if (const ResolvedGroup *group = resolved.groupOf(bookId)) {
                    printWarning(i18n("%1 is selected through the followed %2; use --volumes or remove the %2", bookId, group->description()));
                } else {
                    printWarning(i18n("%1 is not in the selection", bookId));
                }
            }
            Q_EMIT finished(Success);
        });
    });
}

void SelectCommand::addGroup(GroupKind kind, const QString &argument, const QString &volumes)
{
    withKomga([this, kind, argument, volumes]() {
        findGroup(kind, argument, [this, kind, volumes](const QString &id) {
            switch (kind) {
            case GroupKind::Series:
                m_selection->followSeries(id, volumes);
                break;
            case GroupKind::Collection:
                m_selection->followCollection(id);
                break;
            case GroupKind::ReadList:
                m_selection->followReadList(id);
                break;
            }
            if (!m_selection->save()) {
                fail(UsageError, m_selection->errorString());
                return;
            }

            withResolvedSelection([this, id](const ResolvedSelection &resolved) {
                for (const ResolvedGroup &group : resolved.groups) {
                    if (group.id != id) {
                        continue;
                    }
                    const int skipped = std::count_if(group.books.cbegin(), group.books.cend(), [](const KomgaBook &book) {
                        return !isSupportedMediaType(book.mediaType) || book.mediaStatus != MediaStatusReady;
                    });
                    QString line = i18np("Following %2: 1 book", "Following %2: %1 books", group.books.size(), group.description());
                    if (!group.volumes.isEmpty()) {
                        line += i18n(" (volumes %1)", group.volumes);
                    }
                    printLine(line + QLatin1Char('.'));
                    if (skipped > 0) {
                        printWarning(i18np("1 of them will be skipped by sync (unsupported format or not analyzed by Komga)",
                                           "%1 of them will be skipped by sync (unsupported format or not analyzed by Komga)",
                                           skipped));
                    }
                }
                printLine(i18np("Effective selection: 1 book.", "Effective selection: %1 books.", resolved.bookIds.size()));
                Q_EMIT finished(Success);
            });
        });
    });
}

void SelectCommand::removeGroup(GroupKind kind, const QString &argument)
{
    const auto unfollow = [this, kind](const QString &id) {
        switch (kind) {
        case GroupKind::Series:
            return m_selection->unfollowSeries(id);
        case GroupKind::Collection:
            return m_selection->unfollowCollection(id);
        case GroupKind::ReadList:
            return m_selection->unfollowReadList(id);
        }
        return false;
    };
    const auto done = [this](bool changed, const QString &argument) {
        if (!changed) {
            printWarning(i18n("\"%1\" is not followed", argument));
            Q_EMIT finished(Success);
            return;
        }
        if (!m_selection->save()) {
            fail(UsageError, m_selection->errorString());
            return;
        }
        printLine(i18n("Stopped following \"%1\". Books it selected leave the Kobo at the next sync, unless selected otherwise.", argument));
        Q_EMIT finished(Success);
    };

    // A followed id is removed without asking Komga, so that a group deleted in Komga can
    // still be removed.
    if (unfollow(argument.trimmed())) {
        done(true, argument);
        return;
    }
    withKomga([this, kind, argument, unfollow, done]() {
        findGroup(kind, argument, [argument, unfollow, done](const QString &id) {
            done(unfollow(id), argument);
        });
    });
}

void SelectCommand::findGroup(GroupKind kind, const QString &argument, std::function<void(const QString &id)> found)
{
    // A shelf series is given by its id (kobold shelf series lists them).
    if (kind == GroupKind::Series && ShelfLibrary::isShelfId(argument)) {
        m_client->fetchOneSeries(argument, [this, argument, found](const KomgaResult<KomgaSeries> &series) {
            if (!series.ok()) {
                fail(UsageError, i18n("no series named \"%1\"", argument));
                return;
            }
            found(argument);
        });
        return;
    }
    withLibrary([this, kind, argument, found](const QString &libraryId) {
        const auto report = [this, argument](auto resolution, const QString &kindName, auto describe) {
            using Status = typename decltype(resolution)::Status;
            if (resolution.status == Status::Found) {
                return true;
            }
            if (resolution.status == Status::NotFound) {
                fail(UsageError, i18n("no %1 named \"%2\"", kindName, argument));
            } else {
                QStringList candidates;
                for (const auto &item : resolution.matches) {
                    candidates.append(describe(item));
                }
                fail(UsageError, i18n("several %1 named \"%2\": %3; pass an id or --library", kindName, argument, candidates.join(QLatin1String(", "))));
            }
            return false;
        };

        switch (kind) {
        case GroupKind::Series:
            m_client->fetchSeries(libraryId, [this, argument, found, report](const KomgaResult<QList<KomgaSeries>> &series) {
                if (!series.ok()) {
                    fail(KomgaFailure, series.error.message);
                    return;
                }
                const auto resolution = Browsing::resolveSeries(series.value, argument);
                if (report(resolution, i18n("series"), [](const KomgaSeries &item) {
                        return item.id;
                    })) {
                    found(resolution.matches.first().id);
                }
            });
            break;
        case GroupKind::Collection:
            m_client->fetchCollections(libraryId, [this, argument, found, report](const KomgaResult<QList<KomgaCollection>> &collections) {
                if (!collections.ok()) {
                    fail(KomgaFailure, collections.error.message);
                    return;
                }
                const auto resolution = Browsing::resolveCollection(collections.value, argument);
                if (report(resolution, i18n("collection"), [](const KomgaCollection &item) {
                        return item.id;
                    })) {
                    found(resolution.matches.first().id);
                }
            });
            break;
        case GroupKind::ReadList:
            m_client->fetchReadLists(libraryId, [this, argument, found, report](const KomgaResult<QList<KomgaReadList>> &readLists) {
                if (!readLists.ok()) {
                    fail(KomgaFailure, readLists.error.message);
                    return;
                }
                const auto resolution = Browsing::resolveReadList(readLists.value, argument);
                if (report(resolution, i18n("read list"), [](const KomgaReadList &item) {
                        return item.id;
                    })) {
                    found(resolution.matches.first().id);
                }
            });
            break;
        }
    });
}

void SelectCommand::addFiles(const QStringList &paths)
{
    // Every file is checked before anything changes (SPEC-LOCAL.md §2).
    QStringList absolutePaths;
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (!info.isFile()) {
            fail(UsageError, i18n("%1 is not a file", path));
            return;
        }
        if (LocalFiles::mediaTypeFor(path).isEmpty()) {
            fail(UsageError, i18n("%1 is not an EPUB, PDF, CBZ or CBR file", path));
            return;
        }
        absolutePaths.append(info.absoluteFilePath());
    }
    for (const QString &path : std::as_const(absolutePaths)) {
        printLine(m_selection->addLocalFile(path) ? i18n("Added: %1", path) : i18n("Already selected: %1", path));
    }
    if (!m_selection->save()) {
        fail(UsageError, m_selection->errorString());
        return;
    }
    Q_EMIT finished(Success);
}

void SelectCommand::removeFiles(const QStringList &paths)
{
    for (const QString &path : paths) {
        const QString absolutePath = QFileInfo(path).absoluteFilePath();
        if (m_selection->removeLocalFile(absolutePath)) {
            printLine(i18n("Removed: %1 (it leaves the Kobo at the next sync, after confirmation)", absolutePath));
        } else {
            printWarning(i18n("%1 is not in the selection", absolutePath));
        }
    }
    if (!m_selection->save()) {
        fail(UsageError, m_selection->errorString());
        return;
    }
    Q_EMIT finished(Success);
}

void SelectCommand::printLocalFiles()
{
    const QStringList files = m_selection->localFiles();
    if (files.isEmpty()) {
        return;
    }
    printLine(i18n("Local files:"));
    for (const QString &path : files) {
        printLine(QStringLiteral("  ") + (QFileInfo::exists(path) ? path : i18n("%1 (missing on the computer)", path)));
    }
}

void SelectCommand::list()
{
    if (m_selection->bookIds().isEmpty() && !m_selection->hasFollowedGroups()) {
        if (!m_selection->localFiles().isEmpty()) {
            printLocalFiles();
            Q_EMIT finished(Success);
            return;
        }
        printLine(i18n("The selection is empty. Find books with: kobold select search <text> or kobold library series"));
        Q_EMIT finished(Success);
        return;
    }

    withKomga([this]() {
        withResolvedSelection([this](const ResolvedSelection &resolved) {
            auto hiddenItems = std::make_shared<int>(0);
            const auto printGroups = [this, &resolved, hiddenItems](ResolvedGroup::Kind kind, const QString &title) {
                printLine(title);
                bool any = false;
                for (const ResolvedGroup &group : resolved.groups) {
                    if (group.kind != kind) {
                        continue;
                    }
                    if (kind == ResolvedGroup::Kind::Series && m_hiddenSeries.contains(group.id)) {
                        ++*hiddenItems;
                        continue;
                    }
                    any = true;
                    QString line = QStringLiteral("  %1  %2").arg(group.id.leftJustified(13), group.name.leftJustified(28));
                    if (!group.volumes.isEmpty()) {
                        line += QStringLiteral("  ") + i18n("volumes %1", group.volumes).leftJustified(14);
                    }
                    printLine(line + QStringLiteral("  ") + i18np("1 book", "%1 books", group.books.size()));
                }
                if (!any) {
                    printLine(QStringLiteral("  ") + i18n("(none)"));
                }
            };
            printGroups(ResolvedGroup::Kind::Series, i18n("Series:"));
            printGroups(ResolvedGroup::Kind::Collection, i18n("Collections:"));
            printGroups(ResolvedGroup::Kind::ReadList, i18n("Read lists:"));

            const QStringList bookIds = resolved.directBookIds;
            const qsizetype effective = resolved.bookIds.size();
            const auto finish = [this, effective, hiddenItems]() {
                printLocalFiles();
                if (*hiddenItems > 0) {
                    printLine(i18np("1 item hidden", "%1 items hidden", *hiddenItems));
                }
                printLine(i18np("Effective selection: 1 book.", "Effective selection: %1 books.", effective));
                Q_EMIT finished(Success);
            };
            printLine(i18n("Books:"));
            if (bookIds.isEmpty()) {
                printLine(QStringLiteral("  ") + i18n("(none)"));
                finish();
                return;
            }
            fetchBooks(bookIds, [this, bookIds, finish, hiddenItems](const QHash<QString, KomgaBook> &books, const QHash<QString, KomgaError> &errors) {
                for (const QString &bookId : bookIds) {
                    if (books.contains(bookId) && m_hiddenSeries.contains(books.value(bookId).seriesId)) {
                        ++*hiddenItems;
                    } else if (books.contains(bookId)) {
                        printBook(books.value(bookId), false);
                    } else {
                        printLine(
                            QStringLiteral("  %1  %2  (%3)").arg(bookId.leftJustified(13), QStringLiteral("?").leftJustified(5), errors.value(bookId).message));
                    }
                }
                finish();
            });
        });
    });
}

void SelectCommand::search(const QString &text)
{
    withKomga([this, text]() {
        withLibrary([this, text](const QString &libraryId) {
            withResolvedSelection([this, text, libraryId](const ResolvedSelection &resolved) {
                m_client->searchBooks(
                    text,
                    SearchResultLimit,
                    [this, resolved](const KomgaResult<QList<KomgaBook>> &result) {
                        if (!result.ok()) {
                            fail(KomgaFailure, result.error.message);
                            return;
                        }
                        if (result.value.isEmpty()) {
                            printLine(i18n("No book matches."));
                            Q_EMIT finished(Success);
                            return;
                        }
                        for (const KomgaBook &book : result.value) {
                            if (!m_hiddenSeries.contains(book.seriesId)) {
                                printBook(book, resolved.contains(book.id));
                            }
                        }
                        printLine(i18n("* = already selected. Add books with: kobold select add <id>..."));
                        Q_EMIT finished(Success);
                    },
                    libraryId);
            });
        });
    });
}

void SelectCommand::withLibrary(std::function<void(const QString &libraryId)> done)
{
    if (m_options.library.isEmpty()) {
        done(QString());
        return;
    }
    m_client->fetchLibraries([this, done](const KomgaResult<QList<KomgaLibrary>> &libraries) {
        if (!libraries.ok()) {
            fail(KomgaFailure, libraries.error.message);
            return;
        }
        const auto resolution = Browsing::resolveLibrary(libraries.value, m_options.library);
        if (resolution.status != Browsing::Resolution<KomgaLibrary>::Status::Found) {
            fail(UsageError,
                 resolution.status == Browsing::Resolution<KomgaLibrary>::Status::NotFound
                     ? i18n("no library named \"%1\"", m_options.library)
                     : i18n("several libraries named \"%1\"; pass an id", m_options.library));
            return;
        }
        done(resolution.matches.first().id);
    });
}

void SelectCommand::withResolvedSelection(std::function<void(const ResolvedSelection &)> done)
{
    resolveSelection(m_client, *m_selection, [this, done](const ResolvedSelection &resolved) {
        if (!resolved.ok()) {
            fail(resolved.status == ResolvedSelection::Status::GroupMissing ? UsageError : KomgaFailure, resolved.errorString);
            return;
        }
        done(resolved);
    });
}

void SelectCommand::fetchBooks(const QStringList &bookIds, std::function<void(const QHash<QString, KomgaBook> &, const QHash<QString, KomgaError> &)> done)
{
    struct State {
        QHash<QString, KomgaBook> books;
        QHash<QString, KomgaError> errors;
        qsizetype pending = 0;
    };
    auto state = std::make_shared<State>();
    state->pending = bookIds.size();
    for (const QString &bookId : bookIds) {
        m_client->fetchBook(bookId, [state, bookId, done](const KomgaResult<KomgaBook> &result) {
            if (result.ok()) {
                state->books.insert(bookId, result.value);
            } else {
                state->errors.insert(bookId, result.error);
            }
            if (--state->pending == 0) {
                done(state->books, state->errors);
            }
        });
    }
}

void SelectCommand::withKomga(std::function<void()> onReady)
{
    openKomgaSession(
        m_settings,
        m_apiKeyStore,
        m_network,
        this,
        [this, onReady](KomgaClient *client) {
            m_client = client;
            loadHiddenSeries(m_settings, m_client, m_options.unlocked, [this, onReady](const QList<KomgaSeries> &adult) {
                m_hiddenSeries = seriesIds(adult);
                onReady();
            });
        },
        [this](int exitCode, const QString &message) {
            fail(exitCode, message);
        });
}

void SelectCommand::printBook(const KomgaBook &book, bool selected)
{
    printLine(bookLine(book, selected));
}

void SelectCommand::fail(int exitCode, const QString &message)
{
    printError(message);
    Q_EMIT finished(exitCode);
}

} // namespace Kobold::Cli
