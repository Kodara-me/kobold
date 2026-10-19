// SPDX-License-Identifier: MIT

#pragma once

#include "komgabook.h"
#include "komgatypes.h"

#include <QList>
#include <QSet>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;
class QUrlQuery;

namespace Kobold
{

struct KomgaError {
    enum class Kind {
        None,
        // Connection failure, DNS error, timeout.
        Network,
        // HTTP 401 or 403: missing, invalid or insufficient API key.
        Unauthorized,
        // HTTP 404.
        NotFound,
        // Any other non-2xx HTTP status.
        Http,
        // 2xx response whose body could not be parsed.
        InvalidResponse,
        // Local file error while saving a download.
        File,
    };

    Kind kind = Kind::None;
    int httpStatus = 0;
    QString message;

    bool isError() const
    {
        return kind != Kind::None;
    }
};

template<typename T>
struct KomgaResult {
    T value{};
    KomgaError error;

    bool ok() const
    {
        return !error.isError();
    }
};

struct KomgaUser {
    QString id;
    QString email;
    QStringList roles;
};

// An API key of the current user. The key itself is only known in the answer to its
// creation (SPEC-KOBO-SYNC.md §4.2).
struct KomgaApiKey {
    QString id;
    QString comment;
    QString key;
};

// Books that do not come from Komga (shelves, SPEC-SHELF.md §6), answered by KomgaClient like
// Komga ones so that selection, sync and browsing need no special case.
class BookSource
{
public:
    virtual ~BookSource() = default;
    // True for ids this source answers ("shelf:…").
    virtual bool handles(const QString &id) const = 0;
    virtual std::optional<KomgaBook> book(const QString &bookId) const = 0;
    virtual std::optional<KomgaSeries> series(const QString &seriesId) const = 0;
    virtual QList<KomgaBook> seriesBooks(const QString &seriesId) const = 0;
    // The book's file, empty when unknown.
    virtual QString filePath(const QString &bookId) const = 0;
    // JPEG bytes of a cover, empty when none.
    virtual QByteArray cover(const QString &id, bool series) const = 0;
};

// Asynchronous client for the Komga REST API (SPEC.md §3, §9, §11).
//
// Every request carries the X-API-Key header. The key is never logged and never sent
// to another origin (redirects are restricted to the same origin).
// Callbacks run on the client's thread and are not called after the client is destroyed.
class KomgaClient : public QObject
{
    Q_OBJECT

public:
    explicit KomgaClient(QNetworkAccessManager *networkAccessManager, QObject *parent = nullptr);

    QUrl baseUrl() const;
    // Komga server root, e.g. https://komga.example.org. A sub-path is supported.
    void setBaseUrl(const QUrl &baseUrl);
    void setApiKey(const QString &apiKey);
    // Series left out of browsing answers (series lists, books of a series, collection
    // series, read list books, searches), for parental control on the command line
    // (SPEC-PARENTAL.md §4). fetchBook and fetchOneSeries for the sync are not filtered, except
    // that fetchOneSeries reports a hidden series as not found.
    void setHiddenSeries(const QSet<QString> &seriesIds);
    // Answers the ids it handles instead of Komga; not owned.
    void setBookSource(BookSource *source);

    // HTTP basic authentication instead of the API key, for the one request that needs it
    // (SPEC-KOBO-SYNC.md §4.2). Use a dedicated client: cookies are neither stored nor sent,
    // so no session outlives the request.
    void setBasicCredentials(const QString &email, const QString &password);
    // Maximum inactivity before a transfer is aborted. Default: 30 seconds.
    void setTransferTimeout(std::chrono::milliseconds timeout);
    // Items per page for paged lists. Default: 500. Every page is always read.
    void setPageSize(int pageSize);

    // GET /api/v2/users/me
    void fetchCurrentUser(std::function<void(const KomgaResult<KomgaUser> &)> callback);

    // GET /api/v1/books/{id}
    void fetchBook(const QString &bookId, std::function<void(const KomgaResult<KomgaBook> &)> callback);

    // POST /api/v1/books/list with a full-text search, optionally within one library.
    void
    searchBooks(const QString &text, int maxResults, std::function<void(const KomgaResult<QList<KomgaBook>> &)> callback, const QString &libraryId = QString());

    // Library browsing (SPEC-LIBRARY.md §6). An empty libraryId means every library.

    // GET /api/v1/libraries
    void fetchLibraries(std::function<void(const KomgaResult<QList<KomgaLibrary>> &)> callback);
    // POST /api/v1/series/list
    void fetchSeries(const QString &libraryId, std::function<void(const KomgaResult<QList<KomgaSeries>> &)> callback);
    // GET /api/v1/series/{id}
    void fetchOneSeries(const QString &seriesId, std::function<void(const KomgaResult<KomgaSeries> &)> callback);
    // POST /api/v1/series/list?size=1, reading totalElements.
    void countSeries(const QString &libraryId, std::function<void(const KomgaResult<int> &)> callback);
    // POST /api/v1/books/list restricted to one series, by book number.
    void fetchSeriesBooks(const QString &seriesId, std::function<void(const KomgaResult<QList<KomgaBook>> &)> callback);
    // GET /api/v1/collections/{id}
    void fetchOneCollection(const QString &collectionId, std::function<void(const KomgaResult<KomgaCollection> &)> callback);
    // GET /api/v1/readlists/{id}
    void fetchOneReadList(const QString &readListId, std::function<void(const KomgaResult<KomgaReadList> &)> callback);
    // GET /api/v1/collections
    void fetchCollections(const QString &libraryId, std::function<void(const KomgaResult<QList<KomgaCollection>> &)> callback);
    // GET /api/v1/collections/{id}/series
    void fetchCollectionSeries(const QString &collectionId, const QString &libraryId, std::function<void(const KomgaResult<QList<KomgaSeries>> &)> callback);
    // GET /api/v1/readlists
    void fetchReadLists(const QString &libraryId, std::function<void(const KomgaResult<QList<KomgaReadList>> &)> callback);
    // GET /api/v1/readlists/{id}/books
    void fetchReadListBooks(const QString &readListId, const QString &libraryId, std::function<void(const KomgaResult<QList<KomgaBook>> &)> callback);

    // GET /api/v1/series/{id}/thumbnail and /api/v1/books/{id}/thumbnail: image bytes.
    void fetchSeriesThumbnail(const QString &seriesId, std::function<void(const KomgaResult<QByteArray> &)> callback);
    void fetchBookThumbnail(const QString &bookId, std::function<void(const KomgaResult<QByteArray> &)> callback);

    // GET /api/v1/books/{id}/file, streamed to destinationPath. The file only appears once
    // the download is complete; on error nothing is left behind.
    void downloadBookFile(const QString &bookId, const QString &destinationPath, std::function<void(const KomgaError &)> callback);

    // GET /api/v1/genres: every genre of the libraries the user can see.
    void fetchGenres(std::function<void(const KomgaResult<QStringList> &)> callback);

    // API keys and Kobo sync points (SPEC-KOBO-SYNC.md §4, §5.6).
    // POST /api/v2/users/me/api-keys
    void createApiKey(const QString &comment, std::function<void(const KomgaResult<KomgaApiKey> &)> callback);
    // GET /api/v2/users/me/api-keys
    void fetchApiKeys(std::function<void(const KomgaResult<QList<KomgaApiKey>> &)> callback);
    // DELETE /api/v2/users/me/api-keys/{keyId}
    void deleteApiKey(const QString &keyId, std::function<void(const KomgaError &)> callback);
    // DELETE /api/v1/syncpoints/me?key_id={keyId}
    void deleteSyncPoints(const QString &keyId, std::function<void(const KomgaError &)> callback);

    // PATCH /api/v1/books/{id}/read-progress with the given fields.
    void updateReadProgress(const QString &bookId, std::optional<int> page, std::optional<bool> completed, std::function<void(const KomgaError &)> callback);

private:
    using PagesCallback = std::function<void(const KomgaResult<QList<QJsonObject>> &)>;

    // GET path and returns the raw body.
    void fetchBytes(const QString &path, std::function<void(const KomgaResult<QByteArray> &)> callback);
    // GET path and converts the JSON object with T::fromJson.
    template<typename T>
    void fetchObject(const QString &path, std::function<void(const KomgaResult<T> &)> callback);

    QNetworkRequest makeRequest(const QString &path, const QString &query = QString()) const;
    // Reads every page of a paged list. With a body the request is a POST, otherwise a GET.
    void fetchAllPages(const QString &path, const QUrlQuery &query, const std::optional<QJsonObject> &body, PagesCallback callback);
    void fetchPage(const QString &path,
                   const QUrlQuery &query,
                   const std::optional<QJsonObject> &body,
                   int page,
                   std::shared_ptr<QList<QJsonObject>> items,
                   PagesCallback callback);
    QString bookPath(const QString &bookId, const QString &suffix = QString()) const;

    QNetworkAccessManager *m_networkAccessManager;
    QUrl m_baseUrl;
    QByteArray m_apiKey;
    QByteArray m_basicCredentials;
    QSet<QString> m_hiddenSeries;
    BookSource *m_bookSource = nullptr;
    // True (and answers on the next event loop turn) when the book source handles id.
    template<typename T, typename Answer>
    bool fromSource(const QString &id, std::function<void(const KomgaResult<T> &)> &callback, Answer answer);
    std::chrono::milliseconds m_transferTimeout{30000};
    int m_pageSize = 500;
};

} // namespace Kobold
