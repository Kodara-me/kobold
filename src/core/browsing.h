// SPDX-License-Identifier: MIT

#pragma once

#include "komgabook.h"
#include "komgatypes.h"

#include <QList>
#include <QString>

#include <algorithm>
#include <functional>

namespace Kobold::Browsing
{

// Result of designating a library, series, collection or read list by id or name
// (SPEC-LIBRARY.md §4).
template<typename T>
struct Resolution {
    enum class Status {
        Found,
        NotFound,
        Ambiguous,
    };

    Status status = Status::NotFound;
    // One item when found, every candidate when ambiguous.
    QList<T> matches;
};

// An item whose id equals argument wins; otherwise items whose name equals argument,
// ignoring case.
template<typename T, typename IdOf, typename NameOf>
Resolution<T> resolve(const QList<T> &items, const QString &argument, IdOf idOf, NameOf nameOf)
{
    Resolution<T> result;
    const QString wanted = argument.trimmed();
    for (const T &item : items) {
        if (std::invoke(idOf, item) == wanted) {
            result.status = Resolution<T>::Status::Found;
            result.matches = {item};
            return result;
        }
    }
    for (const T &item : items) {
        if (std::invoke(nameOf, item).compare(wanted, Qt::CaseInsensitive) == 0) {
            result.matches.append(item);
        }
    }
    if (result.matches.size() == 1) {
        result.status = Resolution<T>::Status::Found;
    } else if (result.matches.size() > 1) {
        result.status = Resolution<T>::Status::Ambiguous;
    }
    return result;
}

Resolution<KomgaLibrary> resolveLibrary(const QList<KomgaLibrary> &libraries, const QString &argument);
Resolution<KomgaSeries> resolveSeries(const QList<KomgaSeries> &series, const QString &argument);
Resolution<KomgaCollection> resolveCollection(const QList<KomgaCollection> &collections, const QString &argument);
Resolution<KomgaReadList> resolveReadList(const QList<KomgaReadList> &readLists, const QString &argument);

// Sorts by name, ignoring case; ties keep their order.
template<typename T, typename NameOf>
QList<T> sortedByName(QList<T> items, NameOf nameOf)
{
    std::stable_sort(items.begin(), items.end(), [&nameOf](const T &a, const T &b) {
        return std::invoke(nameOf, a).compare(std::invoke(nameOf, b), Qt::CaseInsensitive) < 0;
    });
    return items;
}

// Books of a series: by book number, then title.
QList<KomgaBook> sortSeriesBooks(QList<KomgaBook> books);

// Series of a collection: collection order when ordered (series missing from seriesIds
// come last, by title), otherwise by title.
QList<KomgaSeries> orderCollectionSeries(const KomgaCollection &collection, const QList<KomgaSeries> &series);

// Books of a read list: read list order when ordered (books missing from bookIds come last,
// by title), otherwise by title.
QList<KomgaBook> orderReadListBooks(const KomgaReadList &readList, const QList<KomgaBook> &books);

} // namespace Kobold::Browsing
