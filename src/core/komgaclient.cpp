// SPDX-License-Identifier: MIT

#include "komgaclient.h"
#include "logging.h"

#include "kobold_version.h"

#include <KLocalizedString>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrlQuery>

#include <memory>

namespace Kobold
{

namespace
{
int httpStatus(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

// Maps a finished reply to an error; Kind::None for a 2xx response.
KomgaError errorFromReply(QNetworkReply *reply)
{
    KomgaError error;
    error.httpStatus = httpStatus(reply);

    if (error.httpStatus >= 200 && error.httpStatus < 300 && reply->error() == QNetworkReply::NoError) {
        return error;
    }

    if (error.httpStatus == 401 || error.httpStatus == 403) {
        error.kind = KomgaError::Kind::Unauthorized;
        error.message = i18n("authentication refused by Komga (HTTP %1)", error.httpStatus);
    } else if (error.httpStatus == 404) {
        error.kind = KomgaError::Kind::NotFound;
        error.message = i18n("not found in Komga");
    } else if (error.httpStatus != 0) {
        error.kind = KomgaError::Kind::Http;
        error.message = i18n("Komga returned HTTP %1", error.httpStatus);
    } else if (reply->error() == QNetworkReply::OperationCanceledError) {
        error.kind = KomgaError::Kind::Network;
        error.message = i18n("request to Komga timed out or was cancelled");
    } else {
        error.kind = KomgaError::Kind::Network;
        error.message = i18n("cannot reach Komga: %1", reply->errorString());
    }
    return error;
}

void logFinished(QNetworkReply *reply, const KomgaError &error)
{
    const QByteArray verb = reply->operation() == QNetworkAccessManager::CustomOperation
        ? reply->request().attribute(QNetworkRequest::CustomVerbAttribute).toByteArray()
        : QByteArray();
    // A missing book is an expected answer, reported by the caller.
    if (error.isError() && error.kind != KomgaError::Kind::NotFound) {
        qCWarning(KOBOLD_KOMGA) << "request failed:" << reply->url().path() << verb << error.message;
    } else {
        qCDebug(KOBOLD_KOMGA) << "request done:" << reply->url().path() << verb << error.httpStatus;
    }
}

KomgaError invalidResponse(const QString &detail)
{
    return KomgaError{KomgaError::Kind::InvalidResponse, 0, i18n("invalid response from Komga: %1", detail)};
}

// {"<field>": {"operator": "is", "value": "<value>"}}
QJsonObject isCondition(const QString &field, const QString &value)
{
    return QJsonObject{{field, QJsonObject{{QLatin1String("operator"), QStringLiteral("is")}, {QLatin1String("value"), value}}}};
}

// Converts parsed JSON objects with T::fromJson, dropping objects without an id.
template<typename T>
KomgaResult<QList<T>> convertItems(const KomgaResult<QList<QJsonObject>> &pages)
{
    KomgaResult<QList<T>> result;
    result.error = pages.error;
    for (const QJsonObject &object : pages.value) {
        if (const auto item = T::fromJson(object)) {
            result.value.append(*item);
        }
    }
    return result;
}

std::optional<QJsonObject> parseObject(const QByteArray &data, KomgaError *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        *error = invalidResponse(parseError.error != QJsonParseError::NoError ? parseError.errorString() : i18n("expected a JSON object"));
        return std::nullopt;
    }
    return document.object();
}
}

KomgaClient::KomgaClient(QNetworkAccessManager *networkAccessManager, QObject *parent)
    : QObject(parent)
    , m_networkAccessManager(networkAccessManager)
{
}

QUrl KomgaClient::baseUrl() const
{
    return m_baseUrl;
}

void KomgaClient::setBaseUrl(const QUrl &baseUrl)
{
    m_baseUrl = baseUrl;
}

void KomgaClient::setApiKey(const QString &apiKey)
{
    m_apiKey = apiKey.toUtf8();
}

void KomgaClient::setBasicCredentials(const QString &email, const QString &password)
{
    m_basicCredentials = (email + QLatin1Char(':') + password).toUtf8().toBase64();
}

void KomgaClient::setHiddenSeries(const QSet<QString> &seriesIds)
{
    m_hiddenSeries = seriesIds;
}

void KomgaClient::setBookSource(BookSource *source)
{
    m_bookSource = source;
}

template<typename T, typename Answer>
bool KomgaClient::fromSource(const QString &id, std::function<void(const KomgaResult<T> &)> &callback, Answer answer)
{
    if (!m_bookSource || !m_bookSource->handles(id)) {
        return false;
    }
    KomgaResult<T> result;
    if (!answer(&result.value)) {
        result.error = KomgaError{KomgaError::Kind::NotFound, 404, i18n("not found in the library")};
    }
    QTimer::singleShot(0, this, [result, callback = std::move(callback)]() {
        callback(result);
    });
    return true;
}

void KomgaClient::setTransferTimeout(std::chrono::milliseconds timeout)
{
    m_transferTimeout = timeout;
}

void KomgaClient::setPageSize(int pageSize)
{
    m_pageSize = std::max(1, pageSize);
}

QNetworkRequest KomgaClient::makeRequest(const QString &path, const QString &query) const
{
    QUrl url = m_baseUrl;
    QString basePath = url.path(QUrl::FullyEncoded);
    while (basePath.endsWith(QLatin1Char('/'))) {
        basePath.chop(1);
    }
    url.setPath(basePath + path, QUrl::TolerantMode);
    url.setQuery(query.isEmpty() ? QString() : query);
    url.setFragment(QString());

    QNetworkRequest request(url);
    if (m_basicCredentials.isEmpty()) {
        request.setRawHeader("X-API-Key", m_apiKey);
    } else {
        request.setRawHeader("Authorization", "Basic " + m_basicCredentials);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    }
    request.setRawHeader("Accept", "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("kobold/" KOBOLD_VERSION_STRING));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(m_transferTimeout);
    return request;
}

QString KomgaClient::bookPath(const QString &bookId, const QString &suffix) const
{
    return QStringLiteral("/api/v1/books/") + QString::fromLatin1(QUrl::toPercentEncoding(bookId)) + suffix;
}

void KomgaClient::fetchCurrentUser(std::function<void(const KomgaResult<KomgaUser> &)> callback)
{
    QNetworkReply *reply = m_networkAccessManager->get(makeRequest(QStringLiteral("/api/v2/users/me")));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<KomgaUser> result;
        result.error = errorFromReply(reply);
        logFinished(reply, result.error);
        if (result.ok()) {
            if (const auto object = parseObject(reply->readAll(), &result.error)) {
                result.value.id = object->value(QLatin1String("id")).toString();
                result.value.email = object->value(QLatin1String("email")).toString();
                const QJsonArray roles = object->value(QLatin1String("roles")).toArray();
                for (const QJsonValue &role : roles) {
                    result.value.roles.append(role.toString());
                }
            }
        }
        callback(result);
    });
}

void KomgaClient::fetchBook(const QString &bookId, std::function<void(const KomgaResult<KomgaBook> &)> callback)
{
    if (fromSource<KomgaBook>(bookId, callback, [this, &bookId](KomgaBook *value) {
            const auto book = m_bookSource->book(bookId);
            if (book) {
                *value = *book;
            }
            return book.has_value();
        })) {
        return;
    }
    QNetworkReply *reply = m_networkAccessManager->get(makeRequest(bookPath(bookId)));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<KomgaBook> result;
        result.error = errorFromReply(reply);
        logFinished(reply, result.error);
        if (result.ok()) {
            if (const auto object = parseObject(reply->readAll(), &result.error)) {
                if (const auto book = KomgaBook::fromJson(*object)) {
                    result.value = *book;
                } else {
                    result.error = invalidResponse(i18n("book without id"));
                }
            }
        }
        callback(result);
    });
}

void KomgaClient::searchBooks(const QString &text,
                              int maxResults,
                              std::function<void(const KomgaResult<QList<KomgaBook>> &)> callback,
                              const QString &libraryId)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("size"), QString::number(maxResults));
    QNetworkRequest request = makeRequest(QStringLiteral("/api/v1/books/list"), query.toString(QUrl::FullyEncoded));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QJsonObject search{{QLatin1String("fullTextSearch"), text}};
    if (!libraryId.isEmpty()) {
        search.insert(QLatin1String("condition"), isCondition(QStringLiteral("libraryId"), libraryId));
    }
    const QByteArray body = QJsonDocument(search).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_networkAccessManager->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<QList<KomgaBook>> result;
        result.error = errorFromReply(reply);
        logFinished(reply, result.error);
        if (result.ok()) {
            if (const auto object = parseObject(reply->readAll(), &result.error)) {
                const QJsonArray content = object->value(QLatin1String("content")).toArray();
                for (const QJsonValue &value : content) {
                    if (const auto book = KomgaBook::fromJson(value.toObject()); book && !m_hiddenSeries.contains(book->seriesId)) {
                        result.value.append(*book);
                    }
                }
            }
        }
        callback(result);
    });
}

void KomgaClient::fetchAllPages(const QString &path, const QUrlQuery &query, const std::optional<QJsonObject> &body, PagesCallback callback)
{
    fetchPage(path, query, body, 0, std::make_shared<QList<QJsonObject>>(), std::move(callback));
}

void KomgaClient::fetchPage(const QString &path,
                            const QUrlQuery &query,
                            const std::optional<QJsonObject> &body,
                            int page,
                            std::shared_ptr<QList<QJsonObject>> items,
                            PagesCallback callback)
{
    QUrlQuery pageQuery = query;
    pageQuery.addQueryItem(QStringLiteral("page"), QString::number(page));
    pageQuery.addQueryItem(QStringLiteral("size"), QString::number(m_pageSize));
    QNetworkRequest request = makeRequest(path, pageQuery.toString(QUrl::FullyEncoded));

    QNetworkReply *reply;
    if (body) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        reply = m_networkAccessManager->post(request, QJsonDocument(*body).toJson(QJsonDocument::Compact));
    } else {
        reply = m_networkAccessManager->get(request);
    }

    connect(reply, &QNetworkReply::finished, this, [this, reply, path, query, body, page, items, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<QList<QJsonObject>> result;
        result.error = errorFromReply(reply);
        logFinished(reply, result.error);
        if (!result.ok()) {
            callback(result);
            return;
        }
        const auto object = parseObject(reply->readAll(), &result.error);
        if (!object) {
            callback(result);
            return;
        }

        const QJsonArray content = object->value(QLatin1String("content")).toArray();
        for (const QJsonValue &value : content) {
            items->append(value.toObject());
        }
        // A page without "last" or without content ends the list, so a malformed answer
        // cannot loop forever.
        const bool last = object->value(QLatin1String("last")).toBool(true) || content.isEmpty();
        if (!last) {
            fetchPage(path, query, body, page + 1, items, callback);
            return;
        }
        result.value = *items;
        callback(result);
    });
}

void KomgaClient::fetchLibraries(std::function<void(const KomgaResult<QList<KomgaLibrary>> &)> callback)
{
    QNetworkReply *reply = m_networkAccessManager->get(makeRequest(QStringLiteral("/api/v1/libraries")));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<QList<QJsonObject>> objects;
        objects.error = errorFromReply(reply);
        logFinished(reply, objects.error);
        if (objects.ok()) {
            const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
            if (!document.isArray()) {
                objects.error = invalidResponse(i18n("expected a JSON array of libraries"));
            } else {
                const QJsonArray array = document.array();
                for (const QJsonValue &value : array) {
                    objects.value.append(value.toObject());
                }
            }
        }
        callback(convertItems<KomgaLibrary>(objects));
    });
}

void KomgaClient::fetchSeries(const QString &libraryId, std::function<void(const KomgaResult<QList<KomgaSeries>> &)> callback)
{
    QJsonObject body;
    if (!libraryId.isEmpty()) {
        body.insert(QLatin1String("condition"), isCondition(QStringLiteral("libraryId"), libraryId));
    }
    fetchAllPages(QStringLiteral("/api/v1/series/list"), QUrlQuery(), body, [this, callback = std::move(callback)](const KomgaResult<QList<QJsonObject>> &pages) {
        KomgaResult<QList<KomgaSeries>> result = convertItems<KomgaSeries>(pages);
        result.value.removeIf([this](const KomgaSeries &series) {
            return m_hiddenSeries.contains(series.id);
        });
        callback(result);
    });
}

template<typename T>
void KomgaClient::fetchObject(const QString &path, std::function<void(const KomgaResult<T> &)> callback)
{
    QNetworkReply *reply = m_networkAccessManager->get(makeRequest(path));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<T> result;
        result.error = errorFromReply(reply);
        logFinished(reply, result.error);
        if (result.ok()) {
            if (const auto object = parseObject(reply->readAll(), &result.error)) {
                if (const auto item = T::fromJson(*object)) {
                    result.value = *item;
                } else {
                    result.error = invalidResponse(i18n("object without id"));
                }
            }
        }
        callback(result);
    });
}

void KomgaClient::fetchOneSeries(const QString &seriesId, std::function<void(const KomgaResult<KomgaSeries> &)> callback)
{
    if (fromSource<KomgaSeries>(seriesId, callback, [this, &seriesId](KomgaSeries *value) {
            const auto series = m_bookSource->series(seriesId);
            if (series) {
                *value = *series;
            }
            return series.has_value() && !m_hiddenSeries.contains(seriesId);
        })) {
        return;
    }
    if (m_hiddenSeries.contains(seriesId)) {
        QTimer::singleShot(0, this, [callback = std::move(callback)]() {
            KomgaResult<KomgaSeries> result;
            result.error = KomgaError{KomgaError::Kind::NotFound, 404, i18n("not found in Komga")};
            callback(result);
        });
        return;
    }
    fetchObject<KomgaSeries>(QStringLiteral("/api/v1/series/") + QString::fromLatin1(QUrl::toPercentEncoding(seriesId)), std::move(callback));
}

void KomgaClient::fetchOneCollection(const QString &collectionId, std::function<void(const KomgaResult<KomgaCollection> &)> callback)
{
    fetchObject<KomgaCollection>(QStringLiteral("/api/v1/collections/") + QString::fromLatin1(QUrl::toPercentEncoding(collectionId)), std::move(callback));
}

void KomgaClient::fetchOneReadList(const QString &readListId, std::function<void(const KomgaResult<KomgaReadList> &)> callback)
{
    fetchObject<KomgaReadList>(QStringLiteral("/api/v1/readlists/") + QString::fromLatin1(QUrl::toPercentEncoding(readListId)), std::move(callback));
}

void KomgaClient::countSeries(const QString &libraryId, std::function<void(const KomgaResult<int> &)> callback)
{
    QJsonObject body;
    if (!libraryId.isEmpty()) {
        body.insert(QLatin1String("condition"), isCondition(QStringLiteral("libraryId"), libraryId));
    }
    QNetworkRequest request = makeRequest(QStringLiteral("/api/v1/series/list"), QStringLiteral("size=1"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QNetworkReply *reply = m_networkAccessManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<int> result;
        result.error = errorFromReply(reply);
        logFinished(reply, result.error);
        if (result.ok()) {
            if (const auto object = parseObject(reply->readAll(), &result.error)) {
                result.value = object->value(QLatin1String("totalElements")).toInt();
            }
        }
        callback(result);
    });
}

void KomgaClient::fetchSeriesBooks(const QString &seriesId, std::function<void(const KomgaResult<QList<KomgaBook>> &)> callback)
{
    if (fromSource<QList<KomgaBook>>(seriesId, callback, [this, &seriesId](QList<KomgaBook> *value) {
            if (!m_bookSource->series(seriesId)) {
                return false;
            }
            if (!m_hiddenSeries.contains(seriesId)) {
                *value = m_bookSource->seriesBooks(seriesId);
            }
            return true;
        })) {
        return;
    }
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("sort"), QStringLiteral("metadata.numberSort,asc"));
    const QJsonObject body{{QLatin1String("condition"), isCondition(QStringLiteral("seriesId"), seriesId)}};
    fetchAllPages(QStringLiteral("/api/v1/books/list"), query, body, [this, callback = std::move(callback)](const KomgaResult<QList<QJsonObject>> &pages) {
        KomgaResult<QList<KomgaBook>> result = convertItems<KomgaBook>(pages);
        result.value.removeIf([this](const KomgaBook &book) {
            return m_hiddenSeries.contains(book.seriesId);
        });
        callback(result);
    });
}

void KomgaClient::fetchCollections(const QString &libraryId, std::function<void(const KomgaResult<QList<KomgaCollection>> &)> callback)
{
    QUrlQuery query;
    if (!libraryId.isEmpty()) {
        query.addQueryItem(QStringLiteral("library_id"), libraryId);
    }
    fetchAllPages(QStringLiteral("/api/v1/collections"), query, std::nullopt, [callback = std::move(callback)](const KomgaResult<QList<QJsonObject>> &pages) {
        callback(convertItems<KomgaCollection>(pages));
    });
}

void KomgaClient::fetchCollectionSeries(const QString &collectionId,
                                        const QString &libraryId,
                                        std::function<void(const KomgaResult<QList<KomgaSeries>> &)> callback)
{
    QUrlQuery query;
    if (!libraryId.isEmpty()) {
        query.addQueryItem(QStringLiteral("library_id"), libraryId);
    }
    const QString path = QStringLiteral("/api/v1/collections/") + QString::fromLatin1(QUrl::toPercentEncoding(collectionId)) + QStringLiteral("/series");
    fetchAllPages(path, query, std::nullopt, [this, callback = std::move(callback)](const KomgaResult<QList<QJsonObject>> &pages) {
        KomgaResult<QList<KomgaSeries>> result = convertItems<KomgaSeries>(pages);
        result.value.removeIf([this](const KomgaSeries &series) {
            return m_hiddenSeries.contains(series.id);
        });
        callback(result);
    });
}

void KomgaClient::fetchReadLists(const QString &libraryId, std::function<void(const KomgaResult<QList<KomgaReadList>> &)> callback)
{
    QUrlQuery query;
    if (!libraryId.isEmpty()) {
        query.addQueryItem(QStringLiteral("library_id"), libraryId);
    }
    fetchAllPages(QStringLiteral("/api/v1/readlists"), query, std::nullopt, [callback = std::move(callback)](const KomgaResult<QList<QJsonObject>> &pages) {
        callback(convertItems<KomgaReadList>(pages));
    });
}

void KomgaClient::fetchReadListBooks(const QString &readListId, const QString &libraryId, std::function<void(const KomgaResult<QList<KomgaBook>> &)> callback)
{
    QUrlQuery query;
    if (!libraryId.isEmpty()) {
        query.addQueryItem(QStringLiteral("library_id"), libraryId);
    }
    const QString path = QStringLiteral("/api/v1/readlists/") + QString::fromLatin1(QUrl::toPercentEncoding(readListId)) + QStringLiteral("/books");
    fetchAllPages(path, query, std::nullopt, [this, callback = std::move(callback)](const KomgaResult<QList<QJsonObject>> &pages) {
        KomgaResult<QList<KomgaBook>> result = convertItems<KomgaBook>(pages);
        result.value.removeIf([this](const KomgaBook &book) {
            return m_hiddenSeries.contains(book.seriesId);
        });
        callback(result);
    });
}

void KomgaClient::fetchBytes(const QString &path, std::function<void(const KomgaResult<QByteArray> &)> callback)
{
    QNetworkRequest request = makeRequest(path);
    request.setRawHeader("Accept", "image/*");
    QNetworkReply *reply = m_networkAccessManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<QByteArray> result;
        result.error = errorFromReply(reply);
        if (result.ok()) {
            result.value = reply->readAll();
        }
        callback(result);
    });
}

void KomgaClient::fetchSeriesThumbnail(const QString &seriesId, std::function<void(const KomgaResult<QByteArray> &)> callback)
{
    if (fromSource<QByteArray>(seriesId, callback, [this, &seriesId](QByteArray *value) {
            *value = m_bookSource->cover(seriesId, true);
            return !value->isEmpty();
        })) {
        return;
    }
    fetchBytes(QStringLiteral("/api/v1/series/") + QString::fromLatin1(QUrl::toPercentEncoding(seriesId)) + QStringLiteral("/thumbnail"), std::move(callback));
}

void KomgaClient::fetchBookThumbnail(const QString &bookId, std::function<void(const KomgaResult<QByteArray> &)> callback)
{
    if (fromSource<QByteArray>(bookId, callback, [this, &bookId](QByteArray *value) {
            *value = m_bookSource->cover(bookId, false);
            return !value->isEmpty();
        })) {
        return;
    }
    fetchBytes(bookPath(bookId, QStringLiteral("/thumbnail")), std::move(callback));
}

void KomgaClient::downloadBookFile(const QString &bookId, const QString &destinationPath, std::function<void(const KomgaError &)> callback)
{
    // A shelf book is copied from its folder (SPEC-SHELF.md §6).
    if (m_bookSource && m_bookSource->handles(bookId)) {
        const QString source = m_bookSource->filePath(bookId);
        QTimer::singleShot(0, this, [source, destinationPath, callback = std::move(callback)]() {
            QFile::remove(destinationPath);
            if (source.isEmpty() || !QFile::copy(source, destinationPath)) {
                callback(KomgaError{KomgaError::Kind::File, 0, i18n("cannot read %1", source)});
                return;
            }
            callback(KomgaError{});
        });
        return;
    }
    auto file = std::make_shared<QSaveFile>(destinationPath);
    if (!file->open(QIODevice::WriteOnly)) {
        callback(KomgaError{KomgaError::Kind::File, 0, i18n("cannot write %1: %2", destinationPath, file->errorString())});
        return;
    }

    QNetworkRequest request = makeRequest(bookPath(bookId, QStringLiteral("/file")));
    request.setRawHeader("Accept", "*/*");
    QNetworkReply *reply = m_networkAccessManager->get(request);

    connect(reply, &QNetworkReply::readyRead, this, [reply, file]() {
        // Error bodies are not written; the final status decides what is kept.
        if (httpStatus(reply) >= 300) {
            return;
        }
        const QByteArray chunk = reply->readAll();
        if (file->write(chunk) != chunk.size()) {
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, file, destinationPath, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaError error = errorFromReply(reply);
        if (!error.isError()) {
            const QByteArray rest = reply->readAll();
            if (file->write(rest) != rest.size() || !file->commit()) {
                error = KomgaError{KomgaError::Kind::File, 0, i18n("cannot write %1: %2", destinationPath, file->errorString())};
            }
        } else if (file->error() != QFileDevice::NoError) {
            error = KomgaError{KomgaError::Kind::File, 0, i18n("cannot write %1: %2", destinationPath, file->errorString())};
        }
        if (error.isError()) {
            file->cancelWriting();
        }
        logFinished(reply, error);
        callback(error);
    });
}

namespace
{
std::optional<KomgaApiKey> apiKeyFromJson(const QJsonObject &object)
{
    KomgaApiKey key;
    key.id = object.value(QLatin1String("id")).toString();
    key.comment = object.value(QLatin1String("comment")).toString();
    key.key = object.value(QLatin1String("key")).toString();
    if (key.id.isEmpty()) {
        return std::nullopt;
    }
    return key;
}
}

void KomgaClient::fetchGenres(std::function<void(const KomgaResult<QStringList> &)> callback)
{
    QNetworkReply *reply = m_networkAccessManager->get(makeRequest(QStringLiteral("/api/v1/genres")));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<QStringList> result;
        result.error = errorFromReply(reply);
        logFinished(reply, result.error);
        if (result.ok()) {
            const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
            if (!document.isArray()) {
                result.error = invalidResponse(i18n("expected a JSON array of genres"));
            } else {
                const QJsonArray array = document.array();
                for (const QJsonValue &value : array) {
                    result.value.append(value.toString());
                }
            }
        }
        callback(result);
    });
}

void KomgaClient::createApiKey(const QString &comment, std::function<void(const KomgaResult<KomgaApiKey> &)> callback)
{
    QNetworkRequest request = makeRequest(QStringLiteral("/api/v2/users/me/api-keys"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    const QJsonObject body{{QLatin1String("comment"), comment}};
    QNetworkReply *reply = m_networkAccessManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<KomgaApiKey> result;
        result.error = errorFromReply(reply);
        logFinished(reply, result.error);
        if (result.ok()) {
            if (const auto object = parseObject(reply->readAll(), &result.error)) {
                const auto key = apiKeyFromJson(*object);
                if (key && !key->key.isEmpty()) {
                    result.value = *key;
                } else {
                    result.error = invalidResponse(i18n("API key without id or value"));
                }
            }
        }
        callback(result);
    });
}

void KomgaClient::fetchApiKeys(std::function<void(const KomgaResult<QList<KomgaApiKey>> &)> callback)
{
    QNetworkReply *reply = m_networkAccessManager->get(makeRequest(QStringLiteral("/api/v2/users/me/api-keys")));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        KomgaResult<QList<KomgaApiKey>> result;
        result.error = errorFromReply(reply);
        logFinished(reply, result.error);
        if (result.ok()) {
            const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
            if (!document.isArray()) {
                result.error = invalidResponse(i18n("expected a JSON array of API keys"));
            } else {
                const QJsonArray array = document.array();
                for (const QJsonValue &value : array) {
                    if (const auto key = apiKeyFromJson(value.toObject())) {
                        result.value.append(*key);
                    }
                }
            }
        }
        callback(result);
    });
}

void KomgaClient::deleteApiKey(const QString &keyId, std::function<void(const KomgaError &)> callback)
{
    const QString path = QStringLiteral("/api/v2/users/me/api-keys/") + QString::fromLatin1(QUrl::toPercentEncoding(keyId));
    QNetworkReply *reply = m_networkAccessManager->deleteResource(makeRequest(path));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        const KomgaError error = errorFromReply(reply);
        logFinished(reply, error);
        callback(error);
    });
}

void KomgaClient::deleteSyncPoints(const QString &keyId, std::function<void(const KomgaError &)> callback)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key_id"), keyId);
    QNetworkReply *reply = m_networkAccessManager->deleteResource(makeRequest(QStringLiteral("/api/v1/syncpoints/me"), query.toString(QUrl::FullyEncoded)));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        const KomgaError error = errorFromReply(reply);
        logFinished(reply, error);
        callback(error);
    });
}

void KomgaClient::updateReadProgress(const QString &bookId,
                                     std::optional<int> page,
                                     std::optional<bool> completed,
                                     std::function<void(const KomgaError &)> callback)
{
    // Shelf books have no server to send progress to (SPEC-SHELF.md §6).
    if (m_bookSource && m_bookSource->handles(bookId)) {
        QTimer::singleShot(0, this, [callback = std::move(callback)]() {
            callback(KomgaError{});
        });
        return;
    }
    QJsonObject body;
    if (page) {
        body.insert(QLatin1String("page"), *page);
    }
    if (completed) {
        body.insert(QLatin1String("completed"), *completed);
    }

    QNetworkRequest request = makeRequest(bookPath(bookId, QStringLiteral("/read-progress")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    QNetworkReply *reply = m_networkAccessManager->sendCustomRequest(request, "PATCH", QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() {
        reply->deleteLater();
        const KomgaError error = errorFromReply(reply);
        logFinished(reply, error);
        callback(error);
    });
}

} // namespace Kobold
