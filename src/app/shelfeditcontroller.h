// SPDX-License-Identifier: MIT

#pragma once

#include "metadatalookup.h"
#include "shelfdatabase.h"
#include "shelflibrary.h"
#include "variantlistmodel.h"

#include <QObject>
#include <qqmlregistration.h>

#include <functional>

class QNetworkAccessManager;

namespace Kobold
{

class ApiKeyStore;

// The metadata editors of shelf books and series (SPEC-SHELF.md §4.3, §4.4): one book, several
// books (shared fields), or a series, with the source of each value, the cover choice and the
// online lookup.
class ShelfEditController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application controller")

    // "book", "books" or "series"; empty when nothing is open.
    Q_PROPERTY(QString kind READ kind NOTIFY targetChanged)
    Q_PROPERTY(QString title READ title NOTIFY targetChanged)
    // The edited ids (one book id, several book ids, or one series id).
    Q_PROPERTY(QStringList ids READ ids NOTIFY targetChanged)
    // Rows: field, label, value, source, sourceLabel, manual, mixed, multiline.
    Q_PROPERTY(Kobold::VariantListModel *fields READ fields CONSTANT)
    // Book: pages the cover can come from (comics and PDF); 0 otherwise.
    Q_PROPERTY(int pageCount READ pageCount NOTIFY targetChanged)
    // "file", "page", "image": where the cover comes from now.
    Q_PROPERTY(QString coverChoice READ coverChoice NOTIFY targetChanged)
    Q_PROPERTY(int coverPage READ coverPage NOTIFY targetChanged)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY targetChanged)

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY resultChanged)
    Q_PROPERTY(QString message READ message NOTIFY resultChanged)
    // What the lookup searches for by default.
    Q_PROPERTY(QString lookupText READ lookupText NOTIFY targetChanged)
    // Rows: label, coverUrl, source.
    Q_PROPERTY(Kobold::VariantListModel *results READ results CONSTANT)
    // Rows: field, label, current, found, checked.
    Q_PROPERTY(Kobold::VariantListModel *comparison READ comparison CONSTANT)
    Q_PROPERTY(bool hasComicVineKey READ hasComicVineKey NOTIFY comicVineKeyChanged)

public:
    struct Context {
        std::function<ShelfDatabase *()> database;
        std::function<ShelfLibrary *()> library;
        QNetworkAccessManager *network = nullptr;
        // The keychain entry of the ComicVine key; a real one when null.
        ApiKeyStore *comicVineKeys = nullptr;
        // Called after every change: the application reloads what it shows.
        std::function<void(const QStringList &bookIds, const QStringList &seriesIds)> changed;
        // Tests: the address of every lookup source.
        QUrl lookupBaseUrl;
    };

    explicit ShelfEditController(const Context &context, QObject *parent = nullptr);

    // Tests: an in-memory key store, and a local server for every lookup source.
    void setComicVineKeyStore(ApiKeyStore *store)
    {
        m_context.comicVineKeys = store;
    }
    void setLookupBaseUrl(const QUrl &url);

    QString kind() const
    {
        return m_kind;
    }
    QString title() const;
    QStringList ids() const
    {
        return m_ids;
    }
    VariantListModel *fields() const
    {
        return m_fields;
    }
    int pageCount() const
    {
        return m_pageCount;
    }
    QString coverChoice() const;
    int coverPage() const;
    QString coverUrl() const;
    bool busy() const
    {
        return m_busy;
    }
    QString errorString() const
    {
        return m_errorString;
    }
    QString message() const
    {
        return m_message;
    }
    QString lookupText() const;
    VariantListModel *results() const
    {
        return m_results;
    }
    VariantListModel *comparison() const
    {
        return m_comparison;
    }
    bool hasComicVineKey() const
    {
        return !m_comicVineKey.isEmpty();
    }

    // Opens one book, several books of a shelf, or a series; returns an error message.
    Q_INVOKABLE QString openBooks(const QStringList &bookIds);
    Q_INVOKABLE QString openSeries(const QString &seriesId);
    // Sets manual values from text (field → text); returns an error message.
    Q_INVOKABLE QString save(const QVariantMap &values);
    // Clears the manual value: the next source shows again.
    Q_INVOKABLE QString clear(const QString &field);
    // Cover from a page (0 is the first), an image file, or back to the file's.
    Q_INVOKABLE QString setCoverPage(int page);
    Q_INVOKABLE QString setCoverImage(const QUrl &file);
    Q_INVOKABLE QString resetCover();

    // Online lookup (§4.3): results, then the comparison of one, then the chosen fields.
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void choose(int index);
    Q_INVOKABLE void setCompared(const QString &field, bool checked);
    // copyToVolumes: a series' genres and age also go to its books.
    Q_INVOKABLE void applyLookup(bool copyToVolumes);

    // Settings → Shelves (§5, §4.3).
    Q_INVOKABLE QString exportEdits(const QUrl &file);
    Q_INVOKABLE QString importEdits(const QUrl &file);
    Q_INVOKABLE void setComicVineKey(const QString &key);
    Q_INVOKABLE void loadComicVineKey();

Q_SIGNALS:
    void targetChanged();
    void busyChanged();
    void resultChanged();
    void comicVineKeyChanged();
    // After applyLookup() or a failed search.
    void lookupFinished(bool ok);

private:
    struct Target {
        QList<ShelfBook> books;
        std::optional<ShelfSeries> series;
    };
    Target target() const;
    void rebuild();
    void notify();
    void setBusy(bool busy);
    void setResult(const QString &error, const QString &message = QString());
    ApiKeyStore *keyStore();
    QString saveCover(const BookMetadata &values, const SeriesMetadata &seriesValues, bool clearing);

    Context m_context;
    QString m_kind;
    QStringList m_ids;
    int m_pageCount = 0;
    bool m_busy = false;
    QString m_errorString;
    QString m_message;
    QString m_comicVineKey;
    ApiKeyStore *m_ownKeyStore = nullptr;
    MetadataLookup *m_lookup;
    QList<LookupResult> m_found;
    int m_chosen = -1;
    VariantListModel *m_fields;
    VariantListModel *m_results;
    VariantListModel *m_comparison;
    int m_coverStamp = 0;
};

} // namespace Kobold
