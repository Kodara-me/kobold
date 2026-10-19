// SPDX-License-Identifier: MIT

#pragma once

#include "komgaclient.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>

class QNetworkAccessManager;

namespace Kobold
{
class ApiKeyStore;
class Settings;
}

namespace Kobold::Cli
{

// `kobold library libraries|series|collections|collection|readlists|readlist`
// (SPEC-LIBRARY.md). Read-only.
class LibraryCommand : public QObject
{
    Q_OBJECT

public:
    LibraryCommand(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent = nullptr);

    // arguments: the words after "library"; library: value of --library, or empty.
    // Emits finished() exactly once.
    // kobo, devicePath: whose selection gives the markers (SPEC-DEVICES.md §3).
    void run(const QStringList &arguments, const QString &library, const QString &kobo = QString(), const QString &devicePath = QString());
    // The parental PIN was given: show adult series (SPEC-PARENTAL.md §4).
    void setUnlocked(bool unlocked);

Q_SIGNALS:
    void finished(int exitCode);

private:
    void start();
    void fetchLibrariesAndDispatch();
    void dispatch();
    void listLibraries();
    void listSeries();
    void showSeries(const QString &argument);
    void listCollections();
    void showCollection(const QString &argument);
    void showCollectionSeries(const QList<KomgaSeries> &series, qsizetype index, int books, int selected);
    void listReadLists();
    void showReadList(const QString &argument);

    void printBooks(const QList<KomgaBook> &books, const QString &indent = QString());
    void printBookCount(int books, int selected);
    // "Title" or "Title [Library]" when the server has several libraries.
    QString seriesName(const KomgaSeries &series) const;
    QString libraryName(const QString &libraryId) const;
    bool showLibraries() const;

    // Reports a Komga error (exit code 4) and returns true when result failed.
    template<typename T>
    bool failed(const KomgaResult<T> &result);
    void fail(int exitCode, const QString &message);

    Settings *m_settings;
    ApiKeyStore *m_apiKeyStore;
    QNetworkAccessManager *m_network;
    KomgaClient *m_client = nullptr;
    QStringList m_arguments;
    QString m_libraryArgument;
    QString m_libraryId;
    QList<KomgaLibrary> m_libraries;
    QSet<QString> m_selection;
    bool m_unlocked = false;
    // Adult series hidden while locked.
    QList<KomgaSeries> m_hiddenSeries;
};

} // namespace Kobold::Cli
