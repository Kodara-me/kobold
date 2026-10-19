// SPDX-License-Identifier: MIT

#include "librarycommand.h"
#include "bookformat.h"
#include "console.h"
#include "exitcode.h"
#include "kobotarget.h"
#include "komgasession.h"
#include "parentalfilter.h"

#include "browsing.h"
#include "selectionresolver.h"
#include "selectionstore.h"

#include <KLocalizedString>

#include <memory>

namespace Kobold::Cli
{

using namespace Kobold::Browsing;

namespace
{
const QString Usage = QStringLiteral(
    "usage: kobold library libraries | series [<series>] | collections | collection <collection> | readlists | readlist <readlist> [--library <library>]");

// Error message for an unresolved argument; kind is "series", "collection", "read list" or "library".
template<typename T, typename Describe>
QString resolutionError(const Resolution<T> &resolution, const QString &kind, const QString &argument, Describe describe)
{
    if (resolution.status == Resolution<T>::Status::NotFound) {
        return i18n("no %1 named \"%2\"", kind, argument);
    }
    QStringList candidates;
    for (const T &item : resolution.matches) {
        candidates.append(describe(item));
    }
    return i18n("several %1 named \"%2\": %3; pass an id or --library", kind, argument, candidates.join(QLatin1String(", ")));
}
}

LibraryCommand::LibraryCommand(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_apiKeyStore(apiKeyStore)
    , m_network(network)
{
}

void LibraryCommand::run(const QStringList &arguments, const QString &library, const QString &kobo, const QString &devicePath)
{
    m_arguments = arguments;
    m_libraryArgument = library;

    const QString action = arguments.value(0);
    const qsizetype count = arguments.size();
    const bool valid = (action == QLatin1String("libraries") && count == 1 && library.isEmpty()) || (action == QLatin1String("series") && count >= 1)
        || (action == QLatin1String("collections") && count == 1) || (action == QLatin1String("collection") && count >= 2)
        || (action == QLatin1String("readlists") && count == 1) || (action == QLatin1String("readlist") && count >= 2);
    if (!valid) {
        fail(UsageError, Usage);
        return;
    }

    std::shared_ptr<SelectionStore> selection;
    const KoboTarget target = resolveSelectionTarget(m_settings, kobo, devicePath);
    if (target.ok) {
        selection = std::make_shared<SelectionStore>(target.selectionPath());
    } else {
        printLine(i18n("Note: no selection markers (%1)", target.errorString));
    }
    if (selection && !selection->load()) {
        printWarning(i18n("%1; books are shown without selection markers", selection->errorString()));
        selection.reset();
    }

    openKomgaSession(
        m_settings,
        m_apiKeyStore,
        m_network,
        this,
        [this, selection](KomgaClient *client) {
            m_client = client;
            if (!selection) {
                start();
                return;
            }
            // Markers show the effective selection (SPEC-SELECTION.md §8).
            resolveSelection(m_client, *selection, [this](const ResolvedSelection &resolved) {
                if (resolved.ok()) {
                    m_selection = QSet<QString>(resolved.bookIds.cbegin(), resolved.bookIds.cend());
                } else {
                    printWarning(i18n("%1; books are shown without selection markers", resolved.errorString));
                }
                start();
            });
        },
        [this](int exitCode, const QString &message) {
            fail(exitCode, message);
        });
}

void LibraryCommand::setUnlocked(bool unlocked)
{
    m_unlocked = unlocked;
}

void LibraryCommand::start()
{
    // Selection markers above were resolved unfiltered; listings leave adult series out.
    loadHiddenSeries(m_settings, m_client, m_unlocked, [this](const QList<KomgaSeries> &adult) {
        m_hiddenSeries = adult;
        m_client->setHiddenSeries(seriesIds(adult));
        fetchLibrariesAndDispatch();
    });
}

void LibraryCommand::fetchLibrariesAndDispatch()
{
    m_client->fetchLibraries([this](const KomgaResult<QList<KomgaLibrary>> &libraries) {
        if (failed(libraries)) {
            return;
        }
        m_libraries = libraries.value;
        if (!m_libraryArgument.isEmpty()) {
            const auto resolution = resolveLibrary(m_libraries, m_libraryArgument);
            if (resolution.status != Resolution<KomgaLibrary>::Status::Found) {
                fail(UsageError, resolutionError(resolution, i18n("library"), m_libraryArgument, [](const KomgaLibrary &library) {
                         return library.id;
                     }));
                return;
            }
            m_libraryId = resolution.matches.first().id;
        }
        dispatch();
    });
}

void LibraryCommand::dispatch()
{
    const QString action = m_arguments.first();
    const QString argument = m_arguments.mid(1).join(QLatin1Char(' '));
    if (action == QLatin1String("libraries")) {
        listLibraries();
    } else if (action == QLatin1String("series")) {
        argument.isEmpty() ? listSeries() : showSeries(argument);
    } else if (action == QLatin1String("collections")) {
        listCollections();
    } else if (action == QLatin1String("collection")) {
        showCollection(argument);
    } else if (action == QLatin1String("readlists")) {
        listReadLists();
    } else {
        showReadList(argument);
    }
}

void LibraryCommand::listLibraries()
{
    if (m_libraries.isEmpty()) {
        printLine(i18n("No libraries."));
        Q_EMIT finished(Success);
        return;
    }

    struct State {
        QHash<QString, int> counts;
        qsizetype pending = 0;
        bool failed = false;
    };
    auto state = std::make_shared<State>();
    state->pending = m_libraries.size();
    for (const KomgaLibrary &library : std::as_const(m_libraries)) {
        m_client->countSeries(library.id, [this, state, id = library.id](const KomgaResult<int> &result) {
            if (state->failed) {
                return;
            }
            if (failed(result)) {
                state->failed = true;
                return;
            }
            state->counts.insert(id, result.value);
            if (--state->pending > 0) {
                return;
            }
            for (const KomgaLibrary &library : sortedByName(m_libraries, &KomgaLibrary::name)) {
                // Adult series are left out; a library left with nothing is hidden.
                int hidden = 0;
                for (const KomgaSeries &series : std::as_const(m_hiddenSeries)) {
                    hidden += series.libraryId == library.id ? 1 : 0;
                }
                const int visible = state->counts.value(library.id) - hidden;
                if (hidden > 0 && visible <= 0) {
                    continue;
                }
                printLine(QStringLiteral("  %1  %2  %3").arg(library.id.leftJustified(13), library.name.leftJustified(38), i18np("1 series", "%1 series", visible)));
            }
            Q_EMIT finished(Success);
        });
    }
}

void LibraryCommand::listSeries()
{
    m_client->fetchSeries(m_libraryId, [this](const KomgaResult<QList<KomgaSeries>> &series) {
        if (failed(series)) {
            return;
        }
        if (series.value.isEmpty()) {
            printLine(i18n("No series."));
            Q_EMIT finished(Success);
            return;
        }

        // Selected books per series, from the metadata of each selected book.
        struct State {
            QHash<QString, int> selectedPerSeries;
            qsizetype pending = 0;
        };
        auto state = std::make_shared<State>();
        auto print = [this, all = series.value, state]() {
            for (const KomgaSeries &item : sortedByName(all, &KomgaSeries::title)) {
                const int selected = state->selectedPerSeries.value(item.id);
                QString line = QStringLiteral("  %1  %2  %3")
                                   .arg(item.id.leftJustified(13), seriesName(item).leftJustified(38), i18np("1 book", "%1 books", item.booksCount));
                if (selected > 0) {
                    line += QStringLiteral("  ") + i18n("(%1 selected)", selected);
                }
                printLine(line);
            }
            Q_EMIT finished(Success);
        };

        state->pending = m_selection.size();
        if (state->pending == 0) {
            print();
            return;
        }
        for (const QString &bookId : std::as_const(m_selection)) {
            m_client->fetchBook(bookId, [state, print](const KomgaResult<KomgaBook> &book) {
                if (book.ok()) {
                    ++state->selectedPerSeries[book.value.seriesId];
                }
                if (--state->pending == 0) {
                    print();
                }
            });
        }
    });
}

void LibraryCommand::showSeries(const QString &argument)
{
    m_client->fetchSeries(m_libraryId, [this, argument](const KomgaResult<QList<KomgaSeries>> &series) {
        if (failed(series)) {
            return;
        }
        const auto resolution = resolveSeries(series.value, argument);
        if (resolution.status != Resolution<KomgaSeries>::Status::Found) {
            fail(UsageError, resolutionError(resolution, i18n("series"), argument, [this](const KomgaSeries &item) {
                     return QStringLiteral("%1 (%2)").arg(item.id, libraryName(item.libraryId));
                 }));
            return;
        }

        m_client->fetchSeriesBooks(resolution.matches.first().id, [this](const KomgaResult<QList<KomgaBook>> &books) {
            if (failed(books)) {
                return;
            }
            const QList<KomgaBook> sorted = sortSeriesBooks(books.value);
            printBooks(sorted);
            const int selected = std::count_if(sorted.cbegin(), sorted.cend(), [this](const KomgaBook &book) {
                return m_selection.contains(book.id);
            });
            printBookCount(sorted.size(), selected);
            Q_EMIT finished(Success);
        });
    });
}

void LibraryCommand::listCollections()
{
    m_client->fetchCollections(m_libraryId, [this](const KomgaResult<QList<KomgaCollection>> &collections) {
        if (failed(collections)) {
            return;
        }
        if (collections.value.isEmpty()) {
            printLine(i18n("No collections."));
        }
        const QSet<QString> hidden = seriesIds(m_hiddenSeries);
        for (const KomgaCollection &collection : sortedByName(collections.value, &KomgaCollection::name)) {
            qsizetype visible = 0;
            for (const QString &id : collection.seriesIds) {
                visible += hidden.contains(id) ? 0 : 1;
            }
            if (visible == 0 && !collection.seriesIds.isEmpty()) {
                continue;
            }
            printLine(QStringLiteral("  %1  %2  %3").arg(collection.id.leftJustified(13), collection.name.leftJustified(38), i18np("1 series", "%1 series", visible)));
        }
        Q_EMIT finished(Success);
    });
}

void LibraryCommand::showCollection(const QString &argument)
{
    m_client->fetchCollections(m_libraryId, [this, argument](const KomgaResult<QList<KomgaCollection>> &collections) {
        if (failed(collections)) {
            return;
        }
        const auto resolution = resolveCollection(collections.value, argument);
        if (resolution.status != Resolution<KomgaCollection>::Status::Found) {
            fail(UsageError, resolutionError(resolution, i18n("collection"), argument, [](const KomgaCollection &item) {
                     return item.id;
                 }));
            return;
        }

        const KomgaCollection collection = resolution.matches.first();
        m_client->fetchCollectionSeries(collection.id, m_libraryId, [this, collection](const KomgaResult<QList<KomgaSeries>> &series) {
            if (failed(series)) {
                return;
            }
            showCollectionSeries(orderCollectionSeries(collection, series.value), 0, 0, 0);
        });
    });
}

void LibraryCommand::showCollectionSeries(const QList<KomgaSeries> &series, qsizetype index, int books, int selected)
{
    if (index >= series.size()) {
        if (series.isEmpty()) {
            printLine(i18n("No series."));
        } else {
            printBookCount(books, selected);
        }
        Q_EMIT finished(Success);
        return;
    }

    const KomgaSeries current = series.at(index);
    m_client->fetchSeriesBooks(current.id, [this, series, index, books, selected, current](const KomgaResult<QList<KomgaBook>> &result) {
        if (failed(result)) {
            return;
        }
        const QList<KomgaBook> sorted = sortSeriesBooks(result.value);
        printLine(QStringLiteral("## %1  [%2]").arg(seriesName(current), current.id));
        printBooks(sorted, QStringLiteral("   "));
        const int seriesSelected = std::count_if(sorted.cbegin(), sorted.cend(), [this](const KomgaBook &book) {
            return m_selection.contains(book.id);
        });
        showCollectionSeries(series, index + 1, books + sorted.size(), selected + seriesSelected);
    });
}

void LibraryCommand::listReadLists()
{
    m_client->fetchReadLists(m_libraryId, [this](const KomgaResult<QList<KomgaReadList>> &readLists) {
        if (failed(readLists)) {
            return;
        }
        if (readLists.value.isEmpty()) {
            printLine(i18n("No read lists."));
        }
        for (const KomgaReadList &readList : sortedByName(readLists.value, &KomgaReadList::name)) {
            printLine(QStringLiteral("  %1  %2  %3")
                          .arg(readList.id.leftJustified(13), readList.name.leftJustified(38), i18np("1 book", "%1 books", readList.bookIds.size())));
        }
        Q_EMIT finished(Success);
    });
}

void LibraryCommand::showReadList(const QString &argument)
{
    m_client->fetchReadLists(m_libraryId, [this, argument](const KomgaResult<QList<KomgaReadList>> &readLists) {
        if (failed(readLists)) {
            return;
        }
        const auto resolution = resolveReadList(readLists.value, argument);
        if (resolution.status != Resolution<KomgaReadList>::Status::Found) {
            fail(UsageError, resolutionError(resolution, i18n("read list"), argument, [](const KomgaReadList &item) {
                     return item.id;
                 }));
            return;
        }

        const KomgaReadList readList = resolution.matches.first();
        m_client->fetchReadListBooks(readList.id, m_libraryId, [this, readList](const KomgaResult<QList<KomgaBook>> &books) {
            if (failed(books)) {
                return;
            }
            const QList<KomgaBook> ordered = orderReadListBooks(readList, books.value);
            printBooks(ordered);
            const int selected = std::count_if(ordered.cbegin(), ordered.cend(), [this](const KomgaBook &book) {
                return m_selection.contains(book.id);
            });
            printBookCount(ordered.size(), selected);
            Q_EMIT finished(Success);
        });
    });
}

void LibraryCommand::printBooks(const QList<KomgaBook> &books, const QString &indent)
{
    for (const KomgaBook &book : books) {
        printLine(bookLine(book, m_selection.contains(book.id), indent));
    }
}

void LibraryCommand::printBookCount(int books, int selected)
{
    printLine(i18np("1 book, %2 selected.", "%1 books, %2 selected.", books, selected));
    printLine(i18n("Add books with: kobold select add <id>..."));
}

QString LibraryCommand::seriesName(const KomgaSeries &series) const
{
    return showLibraries() ? QStringLiteral("%1 [%2]").arg(series.title, libraryName(series.libraryId)) : series.title;
}

QString LibraryCommand::libraryName(const QString &libraryId) const
{
    for (const KomgaLibrary &library : m_libraries) {
        if (library.id == libraryId) {
            return library.name;
        }
    }
    return libraryId;
}

bool LibraryCommand::showLibraries() const
{
    return m_libraries.size() > 1;
}

template<typename T>
bool LibraryCommand::failed(const KomgaResult<T> &result)
{
    if (result.ok()) {
        return false;
    }
    fail(KomgaFailure, result.error.message);
    return true;
}

void LibraryCommand::fail(int exitCode, const QString &message)
{
    printError(message);
    Q_EMIT finished(exitCode);
}

} // namespace Kobold::Cli
