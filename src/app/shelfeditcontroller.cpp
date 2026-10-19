// SPDX-License-Identifier: MIT

#include "shelfeditcontroller.h"

#include "apikeystore.h"
#include "shelfcovers.h"
#include "shelfedits.h"

#include <KLocalizedString>

#include <QDir>
#include <QRegularExpression>

namespace Kobold
{

namespace
{
// Fields several books can share in one edit (SPEC-SHELF.md §4.4).
const QStringList SharedFields{QStringLiteral("series"),
                               QStringLiteral("authors"),
                               QStringLiteral("genres"),
                               QStringLiteral("tags"),
                               QStringLiteral("ageRating"),
                               QStringLiteral("publisher"),
                               QStringLiteral("language"),
                               QStringLiteral("readingDirection")};

QString fieldLabel(const QString &field)
{
    static const QHash<QString, KLocalizedString> labels{
        {QStringLiteral("title"), ki18nc("@label:textbox", "Title:")},
        {QStringLiteral("sortTitle"), ki18nc("@label:textbox", "Sort title:")},
        {QStringLiteral("authors"), ki18nc("@label:textbox", "Authors:")},
        {QStringLiteral("series"), ki18nc("@label:textbox", "Series:")},
        {QStringLiteral("number"), ki18nc("@label:textbox", "Volume:")},
        {QStringLiteral("summary"), ki18nc("@label:textbox", "Summary:")},
        {QStringLiteral("publisher"), ki18nc("@label:textbox", "Publisher:")},
        {QStringLiteral("language"), ki18nc("@label:textbox", "Language:")},
        {QStringLiteral("genres"), ki18nc("@label:textbox", "Genres:")},
        {QStringLiteral("tags"), ki18nc("@label:textbox", "Tags:")},
        {QStringLiteral("ageRating"), ki18nc("@label:textbox", "Age rating:")},
        {QStringLiteral("releaseDate"), ki18nc("@label:textbox", "Release date:")},
        {QStringLiteral("isbn"), ki18nc("@label:textbox", "ISBN:")},
        {QStringLiteral("readingDirection"), ki18nc("@label:textbox", "Reading direction (rtl or ltr):")},
        {QStringLiteral("cover"), ki18nc("@label", "Cover:")},
        {QStringLiteral("name"), ki18nc("@label:textbox", "Name:")},
        {QStringLiteral("sortName"), ki18nc("@label:textbox", "Sort name:")},
    };
    return labels.contains(field) ? labels.value(field).toString() : field;
}

QString sourceLabel(const QString &source)
{
    if (source == QLatin1String("manual")) {
        return i18nc("@info value source", "manual");
    }
    if (source == QLatin1String("online")) {
        return i18nc("@info value source", "online");
    }
    if (source == QLatin1String("embedded")) {
        return i18nc("@info value source", "file");
    }
    if (source == QLatin1String("names")) {
        return i18nc("@info value source", "file name");
    }
    if (source == QLatin1String("books")) {
        return i18nc("@info value source", "its books");
    }
    return {};
}

QString seriesValue(const ShelfLibrary &library, const ShelfSeries &series, const QString &field)
{
    const QString edited = ShelfMetadata::fieldText(series.edits, field);
    if (!edited.isEmpty()) {
        return edited;
    }
    const KomgaSeries komga = library.toKomgaSeries(series);
    if (field == QLatin1String("name")) {
        return series.name;
    }
    if (field == QLatin1String("genres")) {
        return komga.genres.join(QLatin1String(", "));
    }
    if (field == QLatin1String("ageRating")) {
        return komga.ageRating >= 0 ? QString::number(komga.ageRating) : QString();
    }
    if (field == QLatin1String("readingDirection")) {
        return komga.readingDirection == QLatin1String("RIGHT_TO_LEFT") ? QStringLiteral("rtl") : QString();
    }
    return {};
}

bool isComic(const ShelfBook &book)
{
    return book.mediaType != QLatin1String("application/epub+zip") && book.mediaType != QLatin1String("application/pdf");
}
}

ShelfEditController::ShelfEditController(const Context &context, QObject *parent)
    : QObject(parent)
    , m_context(context)
    , m_lookup(new MetadataLookup(context.network, this))
    , m_fields(new VariantListModel({"field", "label", "value", "source", "sourceLabel", "manual", "mixed", "multiline"}, this))
    , m_results(new VariantListModel({"label", "coverUrl", "source"}, this))
    , m_comparison(new VariantListModel({"field", "label", "current", "found", "checked"}, this))
{
    if (context.lookupBaseUrl.isValid()) {
        for (const QString &source : {QStringLiteral("openlibrary"), QStringLiteral("openlibrary-covers"), QStringLiteral("google"), QStringLiteral("comicvine")}) {
            m_lookup->setBaseUrl(source, context.lookupBaseUrl);
        }
    }
}

void ShelfEditController::setLookupBaseUrl(const QUrl &url)
{
    for (const QString &source : {QStringLiteral("openlibrary"), QStringLiteral("openlibrary-covers"), QStringLiteral("google"), QStringLiteral("comicvine")}) {
        m_lookup->setBaseUrl(source, url);
    }
}

ShelfEditController::Target ShelfEditController::target() const
{
    Target target;
    ShelfLibrary *library = m_context.library ? m_context.library() : nullptr;
    if (!library) {
        return target;
    }
    if (m_kind == QLatin1String("series")) {
        target.series = library->findSeries(m_ids.value(0));
        if (target.series) {
            target.books = target.series->books;
        }
        return target;
    }
    for (const QString &id : m_ids) {
        if (const auto found = library->findBook(id)) {
            target.books.append(found->second);
            if (m_ids.size() == 1) {
                target.series = found->first;
            }
        }
    }
    return target;
}

QString ShelfEditController::title() const
{
    const Target current = target();
    if (m_kind == QLatin1String("series") && current.series) {
        return current.series->name;
    }
    if (m_kind == QLatin1String("book") && !current.books.isEmpty()) {
        return current.books.first().effective().title;
    }
    return i18np("1 book", "%1 books", current.books.size());
}

QString ShelfEditController::openBooks(const QStringList &bookIds)
{
    ShelfLibrary *library = m_context.library ? m_context.library() : nullptr;
    if (!library || bookIds.isEmpty()) {
        return i18n("no book to edit");
    }
    for (const QString &id : bookIds) {
        if (!library->findBook(id)) {
            return i18n("no book or series %1 (see kobold shelf series and books)", id);
        }
    }
    m_kind = bookIds.size() == 1 ? QStringLiteral("book") : QStringLiteral("books");
    m_ids = bookIds;
    m_pageCount = 0;
    if (bookIds.size() == 1) {
        const auto found = library->findBook(bookIds.first());
        const auto shelf = m_context.database()->shelf(found->second.shelfId);
        if (shelf) {
            m_pageCount = ShelfMetadata::pageCount(QDir(shelf->path).filePath(found->second.path));
        }
    }
    m_results->clear();
    m_comparison->clear();
    m_found.clear();
    setResult(QString());
    rebuild();
    Q_EMIT targetChanged();
    return {};
}

QString ShelfEditController::openSeries(const QString &seriesId)
{
    ShelfLibrary *library = m_context.library ? m_context.library() : nullptr;
    const auto series = library ? library->findSeries(seriesId) : std::nullopt;
    if (!series) {
        return i18n("no book or series %1 (see kobold shelf series and books)", seriesId);
    }
    // A one-shot is its book.
    if (series->key.isEmpty()) {
        return openBooks({ShelfLibrary::bookId(series->shelfId, series->books.first().id)});
    }
    m_kind = QStringLiteral("series");
    m_ids = {seriesId};
    m_pageCount = 0;
    m_results->clear();
    m_comparison->clear();
    m_found.clear();
    setResult(QString());
    rebuild();
    Q_EMIT targetChanged();
    return {};
}

void ShelfEditController::rebuild()
{
    const Target current = target();
    QList<QVariantMap> rows;
    const auto row = [&rows](const QString &field, const QString &value, const QString &source, bool manual, bool mixed) {
        rows.append({{QStringLiteral("field"), field},
                     {QStringLiteral("label"), fieldLabel(field)},
                     {QStringLiteral("value"), value},
                     {QStringLiteral("source"), source},
                     {QStringLiteral("sourceLabel"), value.isEmpty() ? QString() : sourceLabel(source)},
                     {QStringLiteral("manual"), manual},
                     {QStringLiteral("mixed"), mixed},
                     {QStringLiteral("multiline"), field == QLatin1String("summary")}});
    };
    if (m_kind == QLatin1String("series") && current.series) {
        ShelfLibrary *library = m_context.library();
        const SeriesEdit edit = m_context.database()->seriesEdit(current.series->shelfId, current.series->key);
        for (const QString &field : ShelfMetadata::seriesFields()) {
            if (field == QLatin1String("cover")) {
                continue;
            }
            const QString source = ShelfMetadata::hasField(edit.manual, field) ? QStringLiteral("manual")
                : ShelfMetadata::hasField(edit.online, field)                  ? QStringLiteral("online")
                                                                               : QStringLiteral("books");
            row(field, seriesValue(*library, *current.series, field), source, ShelfMetadata::hasField(edit.manual, field), false);
        }
    } else if (m_kind == QLatin1String("book") && !current.books.isEmpty()) {
        const ShelfBook &book = current.books.first();
        const BookMetadata metadata = book.effective();
        for (const QString &field : ShelfMetadata::bookFields()) {
            if (field != QLatin1String("cover")) {
                row(field, ShelfMetadata::fieldText(metadata, field), book.sourceOf(field), ShelfMetadata::hasField(book.manual, field), false);
            }
        }
    } else if (m_kind == QLatin1String("books")) {
        for (const QString &field : SharedFields) {
            QSet<QString> values;
            QSet<QString> sources;
            bool manual = false;
            for (const ShelfBook &book : current.books) {
                values.insert(ShelfMetadata::fieldText(book.effective(), field));
                sources.insert(book.sourceOf(field));
                manual = manual || ShelfMetadata::hasField(book.manual, field);
            }
            const bool mixed = values.size() > 1;
            row(field, mixed ? QString() : values.values().value(0), sources.size() == 1 ? sources.values().first() : QString(), manual, mixed);
        }
    }
    m_fields->setItems(rows);
}

QString ShelfEditController::coverChoice() const
{
    const Target current = target();
    if (m_kind == QLatin1String("series") && current.series) {
        return current.series->edits.coverImage.isEmpty() ? QStringLiteral("file") : QStringLiteral("image");
    }
    if (m_kind != QLatin1String("book") || current.books.isEmpty()) {
        return {};
    }
    const BookMetadata metadata = current.books.first().effective();
    return !metadata.coverImage.isEmpty() ? QStringLiteral("image") : metadata.coverPage >= 0 ? QStringLiteral("page") : QStringLiteral("file");
}

int ShelfEditController::coverPage() const
{
    const Target current = target();
    return m_kind == QLatin1String("book") && !current.books.isEmpty() ? current.books.first().effective().coverPage : -1;
}

QString ShelfEditController::coverUrl() const
{
    if (m_kind == QLatin1String("series")) {
        return QStringLiteral("image://komga/series/%1?v=%2").arg(m_ids.value(0)).arg(m_coverStamp);
    }
    if (m_kind == QLatin1String("book")) {
        return QStringLiteral("image://komga/book/%1?v=%2").arg(m_ids.value(0)).arg(m_coverStamp);
    }
    return {};
}

void ShelfEditController::setBusy(bool busy)
{
    if (m_busy != busy) {
        m_busy = busy;
        Q_EMIT busyChanged();
    }
}

void ShelfEditController::setResult(const QString &error, const QString &message)
{
    m_errorString = error;
    m_message = message;
    Q_EMIT resultChanged();
}

void ShelfEditController::notify()
{
    const Target current = target();
    QStringList bookIds;
    for (const ShelfBook &book : current.books) {
        bookIds.append(ShelfLibrary::bookId(book.shelfId, book.id));
    }
    QStringList seriesIds;
    if (current.series) {
        seriesIds.append(current.series->id);
    }
    ++m_coverStamp;
    if (m_context.changed) {
        m_context.changed(bookIds, seriesIds);
    }
    rebuild();
    Q_EMIT targetChanged();
}

QString ShelfEditController::save(const QVariantMap &values)
{
    const Target current = target();
    if (current.books.isEmpty()) {
        return i18n("no book to edit");
    }
    QString error;
    bool ok = false;
    if (m_kind == QLatin1String("series")) {
        SeriesMetadata metadata;
        QStringList fields;
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (!ShelfMetadata::seriesFields().contains(it.key()) || !ShelfMetadata::setFieldText(&metadata, it.key(), it.value().toString())) {
                return i18n("\"%1\" is not a valid value for %2", it.value().toString(), it.key());
            }
            fields.append(it.key());
        }
        ok = ShelfEdits::setSeriesFields(m_context.database(), current.series->shelfId, current.series->key, ShelfEdits::Layer::Manual, metadata, fields, {}, &error);
    } else {
        BookMetadata metadata;
        QStringList fields;
        const QStringList allowed = m_kind == QLatin1String("books") ? SharedFields : ShelfMetadata::bookFields();
        for (auto it = values.cbegin(); it != values.cend(); ++it) {
            if (!allowed.contains(it.key()) || !ShelfMetadata::setFieldText(&metadata, it.key(), it.value().toString())) {
                return i18n("\"%1\" is not a valid value for %2", it.value().toString(), it.key());
            }
            fields.append(it.key());
        }
        QList<qint64> bookIds;
        for (const ShelfBook &book : current.books) {
            bookIds.append(book.id);
        }
        ok = ShelfEdits::setBookFields(m_context.database(), bookIds, ShelfEdits::Layer::Manual, metadata, fields, {}, &error);
    }
    if (!ok) {
        return error;
    }
    notify();
    return {};
}

QString ShelfEditController::clear(const QString &field)
{
    const Target current = target();
    if (current.books.isEmpty()) {
        return i18n("no book to edit");
    }
    QString error;
    bool ok = false;
    if (m_kind == QLatin1String("series")) {
        ok = ShelfEdits::setSeriesFields(m_context.database(), current.series->shelfId, current.series->key, ShelfEdits::Layer::Manual, {}, {}, {field}, &error);
    } else {
        QList<qint64> bookIds;
        for (const ShelfBook &book : current.books) {
            bookIds.append(book.id);
        }
        ok = ShelfEdits::setBookFields(m_context.database(), bookIds, ShelfEdits::Layer::Manual, {}, {}, {field}, &error);
    }
    if (!ok) {
        return error;
    }
    notify();
    return {};
}

QString ShelfEditController::saveCover(const BookMetadata &values, const SeriesMetadata &seriesValues, bool clearing)
{
    const Target current = target();
    if (current.books.isEmpty()) {
        return i18n("no book to edit");
    }
    const QStringList fields = clearing ? QStringList() : QStringList{QStringLiteral("cover")};
    const QStringList cleared = clearing ? QStringList{QStringLiteral("cover")} : QStringList();
    QString error;
    bool ok = true;
    if (m_kind == QLatin1String("series")) {
        ok = ShelfEdits::setSeriesFields(m_context.database(), current.series->shelfId, current.series->key, ShelfEdits::Layer::Manual, seriesValues, fields, cleared, &error);
        // Back to the first book's cover: the online one goes too.
        if (ok && clearing) {
            ok = ShelfEdits::setSeriesFields(m_context.database(), current.series->shelfId, current.series->key, ShelfEdits::Layer::Online, {}, {}, cleared, &error);
        }
    } else if (m_kind == QLatin1String("book")) {
        const QList<qint64> bookIds{current.books.first().id};
        ok = ShelfEdits::setBookFields(m_context.database(), bookIds, ShelfEdits::Layer::Manual, values, fields, cleared, &error);
        if (ok && clearing) {
            ok = ShelfEdits::setBookFields(m_context.database(), bookIds, ShelfEdits::Layer::Online, {}, {}, cleared, &error);
        }
    }
    if (!ok) {
        return error;
    }
    notify();
    return {};
}

QString ShelfEditController::setCoverPage(int page)
{
    BookMetadata values;
    values.coverPage = page;
    return saveCover(values, {}, false);
}

QString ShelfEditController::setCoverImage(const QUrl &file)
{
    BookMetadata values;
    values.coverImage = file.toLocalFile();
    SeriesMetadata seriesValues;
    seriesValues.coverImage = file.toLocalFile();
    return saveCover(values, seriesValues, false);
}

QString ShelfEditController::resetCover()
{
    return saveCover({}, {}, true);
}

QString ShelfEditController::lookupText() const
{
    const Target current = target();
    if (m_kind == QLatin1String("series") && current.series) {
        return current.series->name;
    }
    if (m_kind != QLatin1String("book") || current.books.isEmpty()) {
        return {};
    }
    const BookMetadata metadata = current.books.first().effective();
    if (!metadata.isbn.isEmpty()) {
        return metadata.isbn;
    }
    if (isComic(current.books.first()) && !metadata.series.isEmpty() && metadata.number) {
        return QStringLiteral("%1 %2").arg(metadata.series, QString::number(*metadata.number));
    }
    return metadata.title;
}

void ShelfEditController::search(const QString &text)
{
    const Target current = target();
    if (current.books.isEmpty() || m_kind == QLatin1String("books")) {
        return;
    }
    LookupQuery query;
    query.series = m_kind == QLatin1String("series");
    query.comic = isComic(current.books.first());
    static const QRegularExpression isbn(QStringLiteral("^[0-9Xx-]{10,17}$"));
    if (!query.series && isbn.match(text.trimmed()).hasMatch()) {
        query.isbn = QString(text.trimmed()).remove(QLatin1Char('-'));
    } else {
        query.title = text.trimmed();
        if (!query.series) {
            query.author = current.books.first().effective().authors.value(0);
        }
    }
    m_lookup->setComicVineKey(m_comicVineKey);
    m_results->clear();
    m_comparison->clear();
    m_found.clear();
    m_chosen = -1;
    setBusy(true);
    setResult(QString());
    m_lookup->search(query, [this](const QList<LookupResult> &results, const QString &error) {
        setBusy(false);
        m_found = results;
        QList<QVariantMap> rows;
        for (const LookupResult &result : results) {
            rows.append({{QStringLiteral("label"), result.label}, {QStringLiteral("coverUrl"), result.coverUrl.toString()}, {QStringLiteral("source"), result.source}});
        }
        m_results->setItems(rows);
        if (results.isEmpty()) {
            setResult(error.isEmpty() ? i18n("nothing found") : error);
            Q_EMIT lookupFinished(false);
        }
    });
}

void ShelfEditController::choose(int index)
{
    if (index < 0 || index >= m_found.size()) {
        return;
    }
    m_chosen = index;
    const LookupResult &result = m_found.at(index);
    const bool series = m_kind == QLatin1String("series");
    const Target current = target();
    QList<QVariantMap> rows;
    for (const QString &field : series ? ShelfMetadata::seriesFields() : ShelfMetadata::bookFields()) {
        const QString found = field == QLatin1String("cover") ? (result.coverUrl.isValid() ? result.coverUrl.toString() : QString())
            : series                                          ? ShelfMetadata::fieldText(result.series, field)
                                                              : ShelfMetadata::fieldText(result.metadata, field);
        if (found.isEmpty()) {
            continue;
        }
        QString now;
        if (field != QLatin1String("cover")) {
            now = series ? seriesValue(*m_context.library(), *current.series, field) : ShelfMetadata::fieldText(current.books.first().effective(), field);
        }
        // Nothing is checked over a value already there (SPEC-SHELF.md §4.3).
        rows.append({{QStringLiteral("field"), field},
                     {QStringLiteral("label"), fieldLabel(field)},
                     {QStringLiteral("current"), now},
                     {QStringLiteral("found"), found},
                     {QStringLiteral("checked"), now.isEmpty() && field != QLatin1String("cover")}});
    }
    m_comparison->setItems(rows);
}

void ShelfEditController::setCompared(const QString &field, bool checked)
{
    QList<QVariantMap> rows = m_comparison->items();
    for (QVariantMap &row : rows) {
        if (row.value(QStringLiteral("field")).toString() == field) {
            row.insert(QStringLiteral("checked"), checked);
        }
    }
    m_comparison->setItems(rows);
}

void ShelfEditController::applyLookup(bool copyToVolumes)
{
    if (m_chosen < 0 || m_chosen >= m_found.size()) {
        return;
    }
    QStringList chosen;
    for (const QVariantMap &row : m_comparison->items()) {
        if (row.value(QStringLiteral("checked")).toBool()) {
            chosen.append(row.value(QStringLiteral("field")).toString());
        }
    }
    if (chosen.isEmpty()) {
        return;
    }
    const LookupResult result = m_found.at(m_chosen);
    const auto apply = [this, result, chosen, copyToVolumes](const QString &coverImage) {
        const Target current = target();
        BookMetadata bookValues = result.metadata;
        SeriesMetadata seriesValues = result.series;
        QStringList fields = chosen;
        if (fields.contains(QLatin1String("cover"))) {
            if (coverImage.isEmpty()) {
                fields.removeAll(QStringLiteral("cover"));
            }
            bookValues.coverImage = coverImage;
            bookValues.coverPage = -1;
            seriesValues.coverImage = coverImage;
        }
        QString error;
        bool ok = false;
        if (m_kind == QLatin1String("series")) {
            ok = ShelfEdits::setSeriesFields(m_context.database(), current.series->shelfId, current.series->key, ShelfEdits::Layer::Online, seriesValues, fields, {}, &error);
            QStringList shared;
            for (const QString &field : {QStringLiteral("genres"), QStringLiteral("ageRating")}) {
                if (fields.contains(field)) {
                    shared.append(field);
                }
            }
            if (ok && copyToVolumes && !shared.isEmpty()) {
                BookMetadata copied;
                copied.genres = seriesValues.genres;
                copied.ageRating = seriesValues.ageRating;
                QList<qint64> bookIds;
                for (const ShelfBook &book : current.books) {
                    bookIds.append(book.id);
                }
                ok = ShelfEdits::setBookFields(m_context.database(), bookIds, ShelfEdits::Layer::Online, copied, shared, {}, &error);
            }
        } else {
            ok = ShelfEdits::setBookFields(m_context.database(), {current.books.first().id}, ShelfEdits::Layer::Online, bookValues, fields, {}, &error);
        }
        setBusy(false);
        if (!ok) {
            setResult(error);
            Q_EMIT lookupFinished(false);
            return;
        }
        notify();
        setResult(QString(), i18n("Applied: %1.", fields.join(QLatin1String(", "))));
        Q_EMIT lookupFinished(true);
    };
    setBusy(true);
    if (!chosen.contains(QLatin1String("cover"))) {
        apply(QString());
        return;
    }
    m_lookup->fetchCover(result.coverUrl, [apply](const QByteArray &data) {
        QString error;
        apply(ShelfCovers::storeImage(data, &error));
    });
}

QString ShelfEditController::exportEdits(const QUrl &file)
{
    QString error;
    if (!ShelfEdits::exportEdits(*m_context.database(), file.toLocalFile(), &error)) {
        return error;
    }
    return {};
}

QString ShelfEditController::importEdits(const QUrl &file)
{
    const ShelfEdits::ImportReport report = ShelfEdits::importEdits(m_context.database(), file.toLocalFile());
    if (!report.ok) {
        return report.errorString;
    }
    if (m_context.changed) {
        m_context.changed({}, {});
    }
    setResult(QString(),
              i18n("Imported: %1 book(s), %2 series; %3 book(s) not found on these shelves.", report.books, report.series, report.unmatched));
    return {};
}

ApiKeyStore *ShelfEditController::keyStore()
{
    if (m_context.comicVineKeys) {
        return m_context.comicVineKeys;
    }
    if (!m_ownKeyStore) {
        m_ownKeyStore = new ApiKeyStore(ApiKeyStore::DefaultService, ApiKeyStore::ComicVineKey, this);
    }
    return m_ownKeyStore;
}

void ShelfEditController::loadComicVineKey()
{
    keyStore()->read([this](const ApiKeyStore::Result &result) {
        m_comicVineKey = result.status == ApiKeyStore::Result::Status::Ok ? result.apiKey : QString();
        Q_EMIT comicVineKeyChanged();
    });
}

void ShelfEditController::setComicVineKey(const QString &key)
{
    const QString trimmed = key.trimmed();
    const auto done = [this, trimmed](const ApiKeyStore::Result &result) {
        if (result.status == ApiKeyStore::Result::Status::Error) {
            setResult(result.errorString);
            return;
        }
        m_comicVineKey = trimmed;
        Q_EMIT comicVineKeyChanged();
        setResult(QString(), trimmed.isEmpty() ? i18n("ComicVine key removed.") : i18n("ComicVine key stored in the keychain."));
    };
    if (trimmed.isEmpty()) {
        keyStore()->remove(done);
    } else {
        keyStore()->write(trimmed, done);
    }
}

} // namespace Kobold
