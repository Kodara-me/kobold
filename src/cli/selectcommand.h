// SPDX-License-Identifier: MIT

#pragma once

#include "komgaclient.h"
#include "selectionresolver.h"

#include <QHash>
#include <QObject>
#include <QStringList>

#include <memory>

class QNetworkAccessManager;

namespace Kobold
{
class ApiKeyStore;
class SelectionStore;
class Settings;
}

namespace Kobold::Cli
{

// `kobold select add|remove|list|search` (SPEC.md §4, SPEC-SELECTION.md §3).
class SelectCommand : public QObject
{
    Q_OBJECT

public:
    struct Options {
        // --library: narrows name resolution and search.
        QString library;
        // --series, --collection, --readlist: the group to follow or stop following.
        QString series;
        QString collection;
        QString readList;
        // --volumes, with --series only.
        bool hasVolumes = false;
        QString volumes;
        // --kobo and --device: whose selection (SPEC-DEVICES.md §3).
        QString kobo;
        QString devicePath;
        // --unlock with the right PIN: show adult content (SPEC-PARENTAL.md §4).
        bool unlocked = false;
    };

    SelectCommand(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent = nullptr);
    ~SelectCommand() override;

    // arguments: the words after "select". Emits finished() exactly once.
    void run(const QStringList &arguments, const Options &options);

Q_SIGNALS:
    void finished(int exitCode);

private:
    enum class GroupKind {
        Series,
        Collection,
        ReadList,
    };

    void add(const QStringList &bookIds);
    void remove(const QStringList &bookIds);
    void addGroup(GroupKind kind, const QString &argument, const QString &volumes);
    void removeGroup(GroupKind kind, const QString &argument);
    // Resolves argument (id or name) to a group id of that kind.
    void findGroup(GroupKind kind, const QString &argument, std::function<void(const QString &id)> found);
    void addFiles(const QStringList &paths);
    void removeFiles(const QStringList &paths);
    void printLocalFiles();
    void list();
    void search(const QString &text);

    // Resolves --library (empty when not given), then calls done with the library id.
    void withLibrary(std::function<void(const QString &libraryId)> done);
    // Resolves the effective selection; on failure reports it and finishes.
    void withResolvedSelection(std::function<void(const ResolvedSelection &)> done);
    // Fetches every id, then calls done with the books found and the errors by id.
    void fetchBooks(const QStringList &bookIds, std::function<void(const QHash<QString, KomgaBook> &, const QHash<QString, KomgaError> &)> done);
    void withKomga(std::function<void()> onReady);
    void printBook(const KomgaBook &book, bool selected);
    void fail(int exitCode, const QString &message);

    Settings *m_settings;
    ApiKeyStore *m_apiKeyStore;
    QNetworkAccessManager *m_network;
    KomgaClient *m_client = nullptr;
    std::unique_ptr<SelectionStore> m_selection;
    Options m_options;
    // Adult series hidden while locked.
    QSet<QString> m_hiddenSeries;
};

} // namespace Kobold::Cli
