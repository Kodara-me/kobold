// SPDX-License-Identifier: MIT

#include "browsing.h"

namespace Kobold::Browsing
{

namespace
{
QString bookTitle(const KomgaBook &book)
{
    return book.title.isEmpty() ? book.name : book.title;
}

// Orders items by their position in ids; items not in ids follow, by name.
template<typename T, typename NameOf>
QList<T> orderByIds(const QStringList &ids, const QList<T> &items, NameOf nameOf)
{
    QList<T> listed;
    QList<T> unlisted;
    for (const T &item : items) {
        (ids.contains(item.id) ? listed : unlisted).append(item);
    }
    std::stable_sort(listed.begin(), listed.end(), [&ids](const T &a, const T &b) {
        return ids.indexOf(a.id) < ids.indexOf(b.id);
    });
    return listed + sortedByName(unlisted, nameOf);
}
}

Resolution<KomgaLibrary> resolveLibrary(const QList<KomgaLibrary> &libraries, const QString &argument)
{
    return resolve(libraries, argument, &KomgaLibrary::id, &KomgaLibrary::name);
}

Resolution<KomgaSeries> resolveSeries(const QList<KomgaSeries> &series, const QString &argument)
{
    return resolve(series, argument, &KomgaSeries::id, &KomgaSeries::title);
}

Resolution<KomgaCollection> resolveCollection(const QList<KomgaCollection> &collections, const QString &argument)
{
    return resolve(collections, argument, &KomgaCollection::id, &KomgaCollection::name);
}

Resolution<KomgaReadList> resolveReadList(const QList<KomgaReadList> &readLists, const QString &argument)
{
    return resolve(readLists, argument, &KomgaReadList::id, &KomgaReadList::name);
}

QList<KomgaBook> sortSeriesBooks(QList<KomgaBook> books)
{
    std::stable_sort(books.begin(), books.end(), [](const KomgaBook &a, const KomgaBook &b) {
        if (a.numberSort != b.numberSort) {
            return a.numberSort < b.numberSort;
        }
        return bookTitle(a).compare(bookTitle(b), Qt::CaseInsensitive) < 0;
    });
    return books;
}

QList<KomgaSeries> orderCollectionSeries(const KomgaCollection &collection, const QList<KomgaSeries> &series)
{
    if (!collection.ordered) {
        return sortedByName(series, &KomgaSeries::title);
    }
    return orderByIds(collection.seriesIds, series, &KomgaSeries::title);
}

QList<KomgaBook> orderReadListBooks(const KomgaReadList &readList, const QList<KomgaBook> &books)
{
    if (!readList.ordered) {
        return sortedByName(books, bookTitle);
    }
    return orderByIds(readList.bookIds, books, bookTitle);
}

} // namespace Kobold::Browsing
