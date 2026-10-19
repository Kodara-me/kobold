// SPDX-License-Identifier: MIT

#include "metadatalookup.h"
#include "logging.h"

#include <KLocalizedString>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextDocumentFragment>
#include <QUrlQuery>

namespace Kobold
{

namespace
{
constexpr int MaxResults = 10;
constexpr int TimeoutMs = 20000;

QString plainText(const QString &html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText().trimmed();
}

QStringList strings(const QJsonValue &value, int max = 8)
{
    QStringList list;
    for (const QJsonValue &item : value.toArray()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty() && !list.contains(text, Qt::CaseInsensitive)) {
            list.append(text);
        }
        if (list.size() >= max) {
            break;
        }
    }
    return list;
}

// "2004", "2004-05" or "2004-05-01".
QDate partialDate(const QString &text)
{
    const QStringList parts = text.left(10).split(QLatin1Char('-'));
    const int year = parts.value(0).toInt();
    if (year <= 0) {
        return {};
    }
    return QDate(year, std::max(1, parts.value(1).toInt()), std::max(1, parts.value(2).toInt()));
}

// MARC language codes of Open Library.
QString languageCode(const QString &code)
{
    static const QHash<QString, QString> codes{{QStringLiteral("eng"), QStringLiteral("en")},
                                                {QStringLiteral("fre"), QStringLiteral("fr")},
                                                {QStringLiteral("fra"), QStringLiteral("fr")},
                                                {QStringLiteral("ger"), QStringLiteral("de")},
                                                {QStringLiteral("spa"), QStringLiteral("es")},
                                                {QStringLiteral("ita"), QStringLiteral("it")},
                                                {QStringLiteral("jpn"), QStringLiteral("ja")},
                                                {QStringLiteral("por"), QStringLiteral("pt")},
                                                {QStringLiteral("dut"), QStringLiteral("nl")}};
    return codes.value(code, code);
}

QString label(const BookMetadata &metadata)
{
    QString text = metadata.title;
    if (!metadata.authors.isEmpty()) {
        text += QStringLiteral(" — ") + metadata.authors.first();
    }
    if (metadata.releaseDate.isValid()) {
        text += QStringLiteral(" (%1)").arg(metadata.releaseDate.year());
    }
    return text;
}

// A book result also proposes its summary, genres and age to a series.
void fillSeries(LookupResult *result)
{
    result->series.summary = result->metadata.summary;
    result->series.genres = result->metadata.genres;
    result->series.ageRating = result->metadata.ageRating;
}
}

MetadataLookup::MetadataLookup(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

void MetadataLookup::setComicVineKey(const QString &key)
{
    m_comicVineKey = key;
}

void MetadataLookup::setBaseUrl(const QString &source, const QUrl &url)
{
    m_bases.insert(source, url);
}

QUrl MetadataLookup::base(const QString &source) const
{
    if (m_bases.contains(source)) {
        return m_bases.value(source);
    }
    if (source == QLatin1String("openlibrary")) {
        return QUrl(QStringLiteral("https://openlibrary.org"));
    }
    if (source == QLatin1String("openlibrary-covers")) {
        return QUrl(QStringLiteral("https://covers.openlibrary.org"));
    }
    if (source == QLatin1String("google")) {
        return QUrl(QStringLiteral("https://www.googleapis.com"));
    }
    return QUrl(QStringLiteral("https://comicvine.gamespot.com"));
}

void MetadataLookup::get(const QUrl &url, std::function<void(const QByteArray &, const QString &)> done)
{
    QNetworkRequest request(url);
    // ComicVine refuses requests without a user agent.
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Kobold"));
    request.setTransferTimeout(TimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status >= 400) {
            done({}, reply->errorString());
            return;
        }
        done(reply->readAll(), QString());
    });
}

QList<LookupResult> MetadataLookup::parseOpenLibrary(const QByteArray &json, const QUrl &coversBase, bool series)
{
    QList<LookupResult> results;
    const QJsonArray docs = QJsonDocument::fromJson(json).object().value(QLatin1String("docs")).toArray();
    for (const QJsonValue &value : docs) {
        const QJsonObject doc = value.toObject();
        LookupResult result;
        result.source = QStringLiteral("openlibrary");
        result.id = doc.value(QLatin1String("key")).toString();
        BookMetadata &m = result.metadata;
        m.title = doc.value(QLatin1String("title")).toString();
        m.authors = strings(doc.value(QLatin1String("author_name")));
        m.publisher = strings(doc.value(QLatin1String("publisher")), 1).value(0);
        m.language = languageCode(strings(doc.value(QLatin1String("language")), 1).value(0));
        m.genres = strings(doc.value(QLatin1String("subject")), 5);
        const int year = doc.value(QLatin1String("first_publish_year")).toInt();
        if (year > 0) {
            m.releaseDate = QDate(year, 1, 1);
        }
        for (const QString &isbn : strings(doc.value(QLatin1String("isbn")), 20)) {
            if (isbn.size() == 13) {
                m.isbn = isbn;
                break;
            }
        }
        const int cover = doc.value(QLatin1String("cover_i")).toInt();
        if (cover > 0) {
            result.coverUrl = coversBase.resolved(QUrl(QStringLiteral("/b/id/%1-L.jpg").arg(cover)));
        }
        result.label = label(m) + QStringLiteral(" · Open Library");
        if (series) {
            fillSeries(&result);
        }
        if (!m.title.isEmpty()) {
            results.append(result);
        }
    }
    return results.mid(0, MaxResults);
}

QList<LookupResult> MetadataLookup::parseGoogleBooks(const QByteArray &json, bool series)
{
    QList<LookupResult> results;
    const QJsonArray items = QJsonDocument::fromJson(json).object().value(QLatin1String("items")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        const QJsonObject info = item.value(QLatin1String("volumeInfo")).toObject();
        LookupResult result;
        result.source = QStringLiteral("google");
        result.id = item.value(QLatin1String("id")).toString();
        BookMetadata &m = result.metadata;
        m.title = info.value(QLatin1String("title")).toString();
        m.authors = strings(info.value(QLatin1String("authors")));
        m.publisher = info.value(QLatin1String("publisher")).toString();
        m.releaseDate = partialDate(info.value(QLatin1String("publishedDate")).toString());
        m.summary = plainText(info.value(QLatin1String("description")).toString());
        m.genres = strings(info.value(QLatin1String("categories")), 5);
        m.language = info.value(QLatin1String("language")).toString();
        if (info.value(QLatin1String("maturityRating")).toString() == QLatin1String("MATURE")) {
            m.ageRating = 18;
        }
        for (const QJsonValue &identifier : info.value(QLatin1String("industryIdentifiers")).toArray()) {
            if (identifier.toObject().value(QLatin1String("type")).toString() == QLatin1String("ISBN_13")) {
                m.isbn = identifier.toObject().value(QLatin1String("identifier")).toString();
            }
        }
        const QJsonObject images = info.value(QLatin1String("imageLinks")).toObject();
        QString cover = images.value(QLatin1String("thumbnail")).toString();
        if (cover.isEmpty()) {
            cover = images.value(QLatin1String("smallThumbnail")).toString();
        }
        if (!cover.isEmpty()) {
            QUrl url(cover);
            if (url.scheme() == QLatin1String("http")) {
                url.setScheme(QStringLiteral("https"));
            }
            result.coverUrl = url;
        }
        result.label = label(m) + QStringLiteral(" · Google Books");
        if (series) {
            fillSeries(&result);
        }
        if (!m.title.isEmpty()) {
            results.append(result);
        }
    }
    return results.mid(0, MaxResults);
}

QList<LookupResult> MetadataLookup::parseComicVine(const QByteArray &json, bool series)
{
    QList<LookupResult> results;
    const QJsonArray items = QJsonDocument::fromJson(json).object().value(QLatin1String("results")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        LookupResult result;
        result.source = QStringLiteral("comicvine");
        result.id = QString::number(item.value(QLatin1String("id")).toInteger());
        BookMetadata &m = result.metadata;
        const QString description = plainText(item.value(QLatin1String("description")).toString());
        const QString deck = item.value(QLatin1String("deck")).toString();
        m.summary = description.isEmpty() ? deck : description;
        if (series) {
            m.title = item.value(QLatin1String("name")).toString();
            m.publisher = item.value(QLatin1String("publisher")).toObject().value(QLatin1String("name")).toString();
            const int year = item.value(QLatin1String("start_year")).toString().toInt();
            if (year > 0) {
                m.releaseDate = QDate(year, 1, 1);
            }
            result.series.name = m.title;
            result.series.summary = m.summary;
        } else {
            const QString volume = item.value(QLatin1String("volume")).toObject().value(QLatin1String("name")).toString();
            const QString name = item.value(QLatin1String("name")).toString();
            m.series = volume;
            bool ok = false;
            const double number = item.value(QLatin1String("issue_number")).toString().toDouble(&ok);
            if (ok) {
                m.number = number;
            }
            m.title = !name.isEmpty() ? name : ok ? QStringLiteral("%1 #%2").arg(volume, item.value(QLatin1String("issue_number")).toString()) : volume;
            m.releaseDate = partialDate(item.value(QLatin1String("cover_date")).toString());
        }
        const QString cover = item.value(QLatin1String("image")).toObject().value(QLatin1String("medium_url")).toString();
        if (!cover.isEmpty()) {
            result.coverUrl = QUrl(cover);
        }
        result.label = (m.series.isEmpty() || series ? label(m) : QStringLiteral("%1 — %2").arg(m.series, label(m))) + QStringLiteral(" · ComicVine");
        if (!m.title.isEmpty()) {
            results.append(result);
        }
    }
    return results.mid(0, MaxResults);
}

void MetadataLookup::search(const LookupQuery &query, Done done)
{
    if (query.isbn.isEmpty() && query.title.trimmed().isEmpty()) {
        done({}, i18n("give a title or an ISBN to search"));
        return;
    }
    auto results = std::make_shared<QList<LookupResult>>();
    auto errors = std::make_shared<QStringList>();

    // Books: Open Library, then Google Books.
    const auto books = [this, query, results, errors, done]() {
        QUrl openLibrary = base(QStringLiteral("openlibrary")).resolved(QUrl(QStringLiteral("/search.json")));
        QUrlQuery olQuery;
        if (!query.isbn.isEmpty()) {
            olQuery.addQueryItem(QStringLiteral("isbn"), query.isbn);
        } else {
            olQuery.addQueryItem(QStringLiteral("title"), query.title);
            if (!query.author.isEmpty()) {
                olQuery.addQueryItem(QStringLiteral("author"), query.author);
            }
        }
        olQuery.addQueryItem(QStringLiteral("fields"),
                             QStringLiteral("key,title,author_name,first_publish_year,publisher,language,subject,isbn,cover_i"));
        olQuery.addQueryItem(QStringLiteral("limit"), QString::number(MaxResults));
        openLibrary.setQuery(olQuery);
        get(openLibrary, [this, query, results, errors, done](const QByteArray &data, const QString &error) {
            if (!error.isEmpty()) {
                errors->append(QStringLiteral("Open Library: ") + error);
            } else {
                results->append(parseOpenLibrary(data, base(QStringLiteral("openlibrary-covers")), query.series));
            }
            QUrl google = base(QStringLiteral("google")).resolved(QUrl(QStringLiteral("/books/v1/volumes")));
            QString q = !query.isbn.isEmpty() ? QStringLiteral("isbn:") + query.isbn : QStringLiteral("intitle:\"%1\"").arg(query.title);
            if (query.isbn.isEmpty() && !query.author.isEmpty()) {
                q += QStringLiteral(" inauthor:\"%1\"").arg(query.author);
            }
            QUrlQuery googleQuery;
            googleQuery.addQueryItem(QStringLiteral("q"), q);
            googleQuery.addQueryItem(QStringLiteral("maxResults"), QString::number(MaxResults));
            google.setQuery(googleQuery);
            get(google, [query, results, errors, done](const QByteArray &data, const QString &error) {
                if (!error.isEmpty()) {
                    errors->append(QStringLiteral("Google Books: ") + error);
                } else {
                    results->append(parseGoogleBooks(data, query.series));
                }
                done(*results, results->isEmpty() ? (errors->isEmpty() ? i18n("nothing found") : errors->join(QLatin1String("; "))) : QString());
            });
        });
    };

    if (!query.comic || m_comicVineKey.isEmpty()) {
        books();
        return;
    }
    QUrl comicVine = base(QStringLiteral("comicvine")).resolved(QUrl(QStringLiteral("/api/search/")));
    QUrlQuery cvQuery;
    cvQuery.addQueryItem(QStringLiteral("api_key"), m_comicVineKey);
    cvQuery.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
    cvQuery.addQueryItem(QStringLiteral("resources"), query.series ? QStringLiteral("volume") : QStringLiteral("issue"));
    cvQuery.addQueryItem(QStringLiteral("query"), query.title);
    cvQuery.addQueryItem(QStringLiteral("limit"), QString::number(MaxResults));
    comicVine.setQuery(cvQuery);
    get(comicVine, [query, results, books, done](const QByteArray &data, const QString &error) {
        if (error.isEmpty()) {
            results->append(parseComicVine(data, query.series));
        } else {
            // The key is never logged: only the error.
            qCWarning(KOBOLD_KOMGA) << "ComicVine:" << error;
        }
        if (results->isEmpty()) {
            books();
            return;
        }
        done(*results, QString());
    });
}

void MetadataLookup::fetchCover(const QUrl &url, std::function<void(const QByteArray &)> done)
{
    if (!url.isValid()) {
        done({});
        return;
    }
    get(url, [done = std::move(done)](const QByteArray &data, const QString &) {
        done(data);
    });
}

} // namespace Kobold
