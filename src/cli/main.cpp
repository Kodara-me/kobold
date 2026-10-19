// SPDX-License-Identifier: MIT

#include "configcommand.h"
#include "console.h"
#include "devicecommand.h"
#include "kobobookscommand.h"
#include "diagnosticsrunner.h"
#include "exitcode.h"
#include "kobosynccommand.h"
#include "parentalcommand.h"
#include "removalprompt.h"
#include "shelfcommand.h"
#include "kobotarget.h"
#include "librarycommand.h"
#include "progressrunner.h"
#include "selectcommand.h"
#include "syncrunner.h"

#include "apikeystore.h"
#include "comicsetup.h"
#include "deviceidentity.h"
#include "devicelocator.h"
#include "settings.h"

#include "aboutdata.h"
#include "translations.h"

#include <KAboutData>
#include <KLocalizedString>

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QTimer>


using namespace Kobold;
using namespace Kobold::Cli;

namespace
{
// Prints what a runner reports.
void attachConsole(Runner *runner)
{
    QObject::connect(runner, &Runner::output, runner, [](Runner::OutputKind kind, const QString &text) {
        switch (kind) {
        case Runner::OutputKind::Line:
            printLine(text);
            break;
        case Runner::OutputKind::Warning:
            printWarning(text);
            break;
        case Runner::OutputKind::Error:
            printError(text);
            break;
        }
    });
}
}

int main(int argc, char *argv[])
{
    // A command-line tool logs to the terminal, not to the journal, even when piped.
    if (!qEnvironmentVariableIsSet("QT_FORCE_STDERR_LOGGING")) {
        qputenv("QT_FORCE_STDERR_LOGGING", "1");
    }

    QCoreApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("kobold");
    useBuildTreeTranslations();

    KAboutData aboutData = koboldAboutData();
    KAboutData::setApplicationData(aboutData);

    QCommandLineParser parser;
    aboutData.setupCommandLine(&parser);
    parser.setApplicationDescription(aboutData.shortDescription() + QLatin1Char('\n') + QLatin1Char('\n')
                                     + i18n("Commands:\n"
                                            "  config set-url <url>   Set the Komga server URL\n"
                                            "  config set-api-key     Read the Komga API key from standard input and store it\n"
                                            "  config show            Show the configuration (never the API key)\n"
                                            "  config set-profile <p> Set the KCC comic profile of the connected Kobo (--clear removes it)\n"
                                            "  device                 Show the connected Kobo (name, model, profile, selection)\n"
                                            "  device list            List the known Kobos\n"
                                            "  device rename <name>   Rename a Kobo (--kobo to choose which)\n"                                            "  device mount           Mount a Kobo that is plugged in but not mounted\n"
                                            "  device eject           Eject the Kobo so that it indexes its books (--all for every one)\n"
                                            "  device books           List every book of the Kobo (--hidden, --visible, --origin, --search)\n"
                                            "  device hide <id>... | show <id>...\n"
                                            "                         Hide books from the Kobo's library, or show them again\n"
                                            "  device mark-read <id>... | mark-unread <id>...\n"
                                            "                         Change the read status of books on the Kobo\n"
                                            "  device delete <id>...  Delete Komga books and files from the Kobo (undoable; not store books)\n"
                                            "  device history | undo [<batch>]\n"
                                            "                         List or undo the changes made to the Kobo's books\n"
                                            "  device backups | restore <backup>\n"
                                            "                         List or restore the backups of the Kobo's database\n"
                                            "  select search <text>   Search Komga for books\n"
                                            "  select add <id>...     Add books to the selection\n"
                                            "  select add --series <s> [--volumes <range>] | --collection <c> | --readlist <r>\n"
                                            "                         Follow a series, collection or read list\n"
                                            "  select remove <id>...  Remove books from the selection\n"
                                            "  select add-file <path>...  Add files from the computer (EPUB, PDF, CBZ, CBR)\n"
                                            "  select remove-file <path>...\n"
                                            "                         Take files from the computer out of the selection\n"
                                            "  select list            List the selected books\n"
                                            "  library libraries      List the Komga libraries\n"
                                            "  library series [<series>]            List series, or the books of one\n"
                                            "  library collections | collection <c> List collections, or the books of one\n"
                                            "  library readlists | readlist <r>     List read lists, or the books of one\n"
                                            "  sync                   Copy the selected books to the Kobo and remove deselected ones\n"
                                            "  pull-progress          Send reading progress from the Kobo to Komga\n"
                                            "  status                 Show the configuration, the Kobo and the pending changes\n"
                                            "  doctor                 Check dependencies and configuration\n"
                                            "  kobo-sync status       Show the Komga sync endpoint of a Kobo\n"
                                            "  kobo-sync install      Point the Kobo to Komga's own Kobo sync (creates its key)\n"
                                            "  kobo-sync uninstall    Point the Kobo back to the Kobo Store\n"
                                            "  kobo-sync reinstall    Write the endpoint again, after a firmware update\n"
                                            "  kobo-sync set-key      Change the Kobo's Komga key (standard input, or --create)\n"
                                            "  kobo-sync reset        Make the Kobo download every book again from Komga\n"
                                            "  parental status        Show parental control (enable, disable, set-age, add-keyword, remove-keyword)\n"
                                            "  shelf add <name> <folder>  Add a shelf: a folder of books outside Komga, scanned by Kobold\n"
                                            "  shelf scan [<shelf>]   Scan shelves for new, changed and missing books\n"
                                            "  shelf list | series [<shelf>] | books <series> | show <book or series>\n"
                                            "                         Browse shelves (also: remove, rename)\n"
                                            "  shelf edit <book or series>... --set <field>=<value> --unset <field>\n"
                                            "                         Edit metadata; several books share series, authors, genres, age...\n"
                                            "  shelf lookup <book or series>  Find metadata online (Open Library, Google Books, ComicVine)\n"
                                            "  shelf export <file> | import <file>  Move the edits to another computer\n"
                                            "  shelf create-library [<folder>]  Create a Kobold Library folder (in Documents) as a shelf\n"
                                            "  shelf set-comicvine-key  Read the ComicVine API key from standard input and store it"));
    parser.addPositionalArgument(QStringLiteral("command"), i18n("Command to run, see above."), QStringLiteral("<command> [arguments]"));
    const QCommandLineOption noCheckOption(QStringLiteral("no-check"), i18n("config set-api-key: store the key without checking it against Komga."));
    parser.addOption(noCheckOption);
    const QCommandLineOption clearOption(QStringLiteral("clear"), i18n("config set-profile: remove the comic profile override of the Kobo."));
    parser.addOption(clearOption);
    const QCommandLineOption deviceOption(QStringLiteral("device"), i18n("Use the Kobo mounted at <path> instead of detecting it."), QStringLiteral("path"));
    parser.addOption(deviceOption);
    const QCommandLineOption verboseOption(QStringLiteral("verbose"), i18n("Print debug messages."));
    parser.addOption(verboseOption);
    const QCommandLineOption dryRunOption(QStringLiteral("dry-run"), i18n("sync, pull-progress, kobo-sync: print the planned changes without modifying anything."));
    parser.addOption(dryRunOption);
    const QCommandLineOption forceUrlChangeOption(QStringLiteral("force-url-change"), i18n("sync: accept a Kobo previously synced from another Komga server."));
    parser.addOption(forceUrlChangeOption);
    const QCommandLineOption libraryOption(QStringLiteral("library"),
                                           i18n("library, select search: only consider the Komga library <library> (id or name)."),
                                           QStringLiteral("library"));
    parser.addOption(libraryOption);
    const QCommandLineOption seriesOption(QStringLiteral("series"),
                                          i18n("select add/remove: follow or stop following the series <series> (id or name)."),
                                          QStringLiteral("series"));
    parser.addOption(seriesOption);
    const QCommandLineOption collectionOption(QStringLiteral("collection"),
                                              i18n("select add/remove: follow or stop following the collection <collection>."),
                                              QStringLiteral("collection"));
    parser.addOption(collectionOption);
    const QCommandLineOption readListOption(QStringLiteral("readlist"),
                                            i18n("select add/remove: follow or stop following the read list <readlist>."),
                                            QStringLiteral("readlist"));
    parser.addOption(readListOption);
    const QCommandLineOption volumesOption(QStringLiteral("volumes"),
                                           i18n("select add --series: only the volumes in <range>, e.g. 1-10 or 1-3,7,10-."),
                                           QStringLiteral("range"));
    parser.addOption(volumesOption);
    const QCommandLineOption koboOption(QStringLiteral("kobo"),
                                        i18n("The Kobo to work on, by name or serial number (see kobold device list)."),
                                        QStringLiteral("kobo"));
    parser.addOption(koboOption);
    const QCommandLineOption keyFromStdinOption(QStringLiteral("key-from-stdin"), i18n("kobo-sync install: read the Kobo's Komga key from standard input."));
    parser.addOption(keyFromStdinOption);
    const QCommandLineOption createOption(QStringLiteral("create"), i18n("kobo-sync set-key: create a new Komga key instead of reading one."));
    parser.addOption(createOption);
    const QCommandLineOption deleteOldOption(QStringLiteral("delete-old"), i18n("kobo-sync set-key: delete the former key in Komga."));
    parser.addOption(deleteOldOption);
    const QCommandLineOption deleteKeyOption(QStringLiteral("delete-key"), i18n("kobo-sync uninstall: also delete the Kobo's key in Komga."));
    parser.addOption(deleteKeyOption);
    const QCommandLineOption yesOption(QStringLiteral("yes"), i18n("sync, kobo-sync reset, device hide/show/undo/restore: answer every question with its default."));
    parser.addOption(yesOption);
    const QCommandLineOption removeDeselectedOption(QStringLiteral("remove-deselected"),
                                                    i18n("sync: remove the books that left the selection without asking."));
    parser.addOption(removeDeselectedOption);
    const QCommandLineOption removeMissingOption(QStringLiteral("remove-missing"), i18n("sync: remove the books their library (Komga or a shelf) no longer has, without asking."));
    parser.addOption(removeMissingOption);
    const QCommandLineOption ejectOption(QStringLiteral("eject"), i18n("sync: eject the Kobo afterwards when the sync changed it."));
    parser.addOption(ejectOption);
    const QCommandLineOption unlockOption(QStringLiteral("unlock"), i18n("Ask for the parental PIN and show adult content."));
    parser.addOption(unlockOption);
    const QCommandLineOption resetPinOption(QStringLiteral("reset-pin"), i18n("parental disable: forget a lost PIN."));
    parser.addOption(resetPinOption);
    const QCommandLineOption hiddenOption(QStringLiteral("hidden"), i18n("device books: only the hidden books."));
    parser.addOption(hiddenOption);
    const QCommandLineOption visibleOption(QStringLiteral("visible"), i18n("device books: only the books shown on the Kobo."));
    parser.addOption(visibleOption);
    const QCommandLineOption originOption(QStringLiteral("origin"), i18n("device books: only the books from <origin>: store, komga, kobold or other."), QStringLiteral("origin"));
    parser.addOption(originOption);
    const QCommandLineOption searchOption(QStringLiteral("search"), i18n("device books: only the books whose title, author or series contains <text>."), QStringLiteral("text"));
    parser.addOption(searchOption);
    const QCommandLineOption setOption(QStringLiteral("set"), i18n("shelf edit: set <field>=<value> (repeat for several fields)."), QStringLiteral("field=value"));
    parser.addOption(setOption);
    const QCommandLineOption unsetOption(QStringLiteral("unset"), i18n("shelf edit: clear the manual value of <field>, falling back to the next source."), QStringLiteral("field"));
    parser.addOption(unsetOption);
    const QCommandLineOption pickOption(QStringLiteral("pick"), i18n("shelf lookup: use result <n> (1 is the first)."), QStringLiteral("n"));
    parser.addOption(pickOption);
    const QCommandLineOption fieldsOption(QStringLiteral("fields"), i18n("shelf lookup: apply these fields (comma separated, or all)."), QStringLiteral("fields"));
    parser.addOption(fieldsOption);
    const QCommandLineOption allOption(QStringLiteral("all"), i18n("sync, pull-progress: run for every connected Kobo."));
    parser.addOption(allOption);

    parser.process(app);
    aboutData.processCommandLine(&parser);
    if (parser.isSet(verboseOption)) {
        QLoggingCategory::setFilterRules(QStringLiteral("kobold.*.debug=true"));
    }

    const QStringList arguments = parser.positionalArguments();
    const QString command = arguments.value(0);

    if (parser.isSet(allOption) && command != QLatin1String("sync") && command != QLatin1String("pull-progress") && command != QLatin1String("device")) {
        printError(i18n("--all only applies to sync, pull-progress and device eject"));
        return UsageError;
    }

    Settings settings;
    ApiKeyStore apiKeyStore;
    QNetworkAccessManager network;

    if (command == QLatin1String("parental")) {
        return runParentalCommand(&settings, arguments.mid(1), parser.isSet(resetPinOption));
    }
    // Adult content stays hidden unless the PIN is given (SPEC-PARENTAL.md §4).
    const bool unlocked = parser.isSet(unlockOption) && unlockParental(&settings);
    if (parser.isSet(unlockOption) && !unlocked) {
        return UsageError;
    }

    if (command == QLatin1String("config")) {
        ConfigCommand config(&settings, &apiKeyStore, &network);
        QObject::connect(&config, &ConfigCommand::finished, &app, &QCoreApplication::exit, Qt::QueuedConnection);
        QTimer::singleShot(0, &config, [&]() {
            config.run(arguments.mid(1),
                       ConfigCommand::Options{parser.isSet(noCheckOption), parser.isSet(clearOption), parser.value(deviceOption), parser.value(koboOption)});
        });
        return app.exec();
    }

    if (command == QLatin1String("library")) {
        LibraryCommand library(&settings, &apiKeyStore, &network);
        library.setUnlocked(unlocked);
        QObject::connect(&library, &LibraryCommand::finished, &app, &QCoreApplication::exit, Qt::QueuedConnection);
        QTimer::singleShot(0, &library, [&]() {
            library.run(arguments.mid(1), parser.value(libraryOption), parser.value(koboOption), parser.value(deviceOption));
        });
        return app.exec();
    }

    if (command == QLatin1String("select")) {
        SelectCommand select(&settings, &apiKeyStore, &network);
        QObject::connect(&select, &SelectCommand::finished, &app, &QCoreApplication::exit, Qt::QueuedConnection);
        QTimer::singleShot(0, &select, [&]() {
            SelectCommand::Options options;
            options.library = parser.value(libraryOption);
            options.series = parser.value(seriesOption);
            options.collection = parser.value(collectionOption);
            options.readList = parser.value(readListOption);
            options.hasVolumes = parser.isSet(volumesOption);
            options.volumes = parser.value(volumesOption);
            options.kobo = parser.value(koboOption);
            options.devicePath = parser.value(deviceOption);
            options.unlocked = unlocked;
            select.run(arguments.mid(1), options);
        });
        return app.exec();
    }

    if (command == QLatin1String("status") || command == QLatin1String("doctor")) {
        DiagnosticsRunner doctor(command == QLatin1String("status") ? DiagnosticsRunner::Mode::Status : DiagnosticsRunner::Mode::Doctor,
                                 &settings,
                                 &apiKeyStore,
                                 &network);
        attachConsole(&doctor);
        doctor.setUnlocked(unlocked);
        QObject::connect(&doctor, &DiagnosticsRunner::finished, &app, &QCoreApplication::exit, Qt::QueuedConnection);
        QTimer::singleShot(0, &doctor, [&]() {
            doctor.run(parser.value(deviceOption), parser.value(koboOption));
        });
        return app.exec();
    }

    // Removals (SPEC-UNSYNC.md §5): flags answer in advance; --yes takes the defaults; a
    // terminal asks; otherwise nothing is removed.
    // SPEC-MOUNT.md §4: --eject, or the setting.
    const bool ejectAfterSync = !parser.isSet(dryRunOption) && (parser.isSet(ejectOption) || settings.ejectAfterSync());
    const bool removalFlags = parser.isSet(removeDeselectedOption) || parser.isSet(removeMissingOption);
    const auto syncOptions = [&](const QString &devicePath, const QString &kobo) {
        SyncRunner::Options options;
        options.dryRun = parser.isSet(dryRunOption);
        options.forceUrlChange = parser.isSet(forceUrlChangeOption);
        options.devicePath = devicePath;
        options.kobo = kobo;
        options.hideAdult = !unlocked;
        options.askRemovals = !removalFlags && (parser.isSet(yesOption) || isInteractive());
        options.removeDeselected = parser.isSet(removeDeselectedOption);
        options.removeMissing = parser.isSet(removeMissingOption);
        return options;
    };
    const auto attachRemovalPrompts = [&](SyncRunner *sync) {
        QObject::connect(sync, &SyncRunner::removalsToConfirm, sync, [&, sync](const QList<RemovalQuestion> &questions) {
            if (parser.isSet(yesOption)) {
                sync->answerRemovals({});
                return;
            }
            bool cancelled = false;
            const QHash<QString, bool> answers = askRemovals(questions, sync->hiddenContent(), &cancelled);
            cancelled ? sync->cancelRemovals() : sync->answerRemovals(answers);
        });
    };

    // Runs start(mountPoint, finished) for every connected Kobo, one after the other.
    const auto runForAll = [&](const std::function<void(const QString &mountPoint, QObject *context, std::function<void(int)> finished)> &start) {
        if (parser.isSet(koboOption) || parser.isSet(deviceOption)) {
            printError(i18n("--all cannot be combined with --kobo or --device"));
            return int(UsageError);
        }
        const QList<ConnectedKobo> kobos = connectedKobos(&settings);
        if (kobos.isEmpty()) {
            printError(i18n("no Kobo connected"));
            return int(DeviceError);
        }
        QList<int> codes;
        for (const ConnectedKobo &device : kobos) {
            printLine(QStringLiteral("== %1 (%2) at %3").arg(device.kobo.name, device.kobo.serial, device.mountPoint));
            QObject context;
            QEventLoop loop;
            QTimer::singleShot(0, &context, [&]() {
                start(device.mountPoint, &context, [&loop](int code) {
                    QTimer::singleShot(0, &loop, [&loop, code]() {
                        loop.exit(code);
                    });
                });
            });
            codes.append(loop.exec());
            printLine(QString());
        }
        return combineExitCodes(codes);
    };

    if (command == QLatin1String("pull-progress") && parser.isSet(allOption)) {
        return runForAll([&](const QString &mountPoint, QObject *context, std::function<void(int)> finished) {
            auto *pull = new ProgressRunner(&settings, &apiKeyStore, &network, context);
            attachConsole(pull);
            QObject::connect(pull, &ProgressRunner::finished, context, finished);
            pull->run(ProgressRunner::Options{parser.isSet(dryRunOption), mountPoint, QString(), {}, !unlocked});
        });
    }

    if (command == QLatin1String("sync") && parser.isSet(allOption)) {
        return runForAll([&](const QString &mountPoint, QObject *context, std::function<void(int)> finished) {
            auto *sync = new SyncRunner(&settings, &apiKeyStore, &network, context);
            attachConsole(sync);
            QObject::connect(sync, &SyncRunner::finished, context, [&settings, &ejectAfterSync, sync, finished](int code) {
                if (ejectAfterSync && code <= PartialFailure && sync->changedKobo()) {
                    code = std::max(code, ejectKobo(&settings, sync->deviceRoot()));
                }
                finished(code);
            });
            attachRemovalPrompts(sync);
            sync->run(syncOptions(mountPoint, QString()));
        });
    }

    if (command == QLatin1String("pull-progress")) {
        ProgressRunner pull(&settings, &apiKeyStore, &network);
        attachConsole(&pull);
        QObject::connect(&pull, &ProgressRunner::finished, &app, &QCoreApplication::exit, Qt::QueuedConnection);
        QTimer::singleShot(0, &pull, [&]() {
            pull.run(ProgressRunner::Options{parser.isSet(dryRunOption), parser.value(deviceOption), parser.value(koboOption), {}, !unlocked});
        });
        return app.exec();
    }

    if (command == QLatin1String("sync")) {
        SyncRunner sync(&settings, &apiKeyStore, &network);
        attachConsole(&sync);
        attachRemovalPrompts(&sync);
        QObject::connect(&sync, &SyncRunner::finished, &app, &QCoreApplication::exit, Qt::QueuedConnection);
        QTimer::singleShot(0, &sync, [&]() {
            sync.run(syncOptions(parser.value(deviceOption), parser.value(koboOption)));
        });
        const int code = app.exec();
        if (ejectAfterSync && code <= PartialFailure && sync.changedKobo()) {
            return std::max(code, ejectKobo(&settings, sync.deviceRoot()));
        }
        return code;
    }

    if (command == QLatin1String("shelf")) {
        ShelfCommandOptions options;
        options.unlocked = unlocked;
        options.sets = parser.values(setOption);
        options.unsets = parser.values(unsetOption);
        options.pick = parser.value(pickOption).toInt();
        options.fields = parser.value(fieldsOption).split(QLatin1Char(','), Qt::SkipEmptyParts);
        options.yes = parser.isSet(yesOption);
        options.network = &network;
        return runShelfCommand(&settings, arguments.mid(1), options);
    }

    if (command == QLatin1String("kobo-sync")) {
        KoboSyncCommandOptions options;
        options.dryRun = parser.isSet(dryRunOption);
        options.keyFromStdin = parser.isSet(keyFromStdinOption);
        options.create = parser.isSet(createOption);
        options.deleteOld = parser.isSet(deleteOldOption);
        options.deleteKey = parser.isSet(deleteKeyOption);
        options.yes = parser.isSet(yesOption);
        options.kobo = parser.value(koboOption);
        options.devicePath = parser.value(deviceOption);
        return runKoboSyncCommand(&settings, &apiKeyStore, &network, arguments.mid(1), options);
    }

    if (command == QLatin1String("device") && isKoboBooksCommand(arguments.value(1))) {
        KoboBooksCommandOptions options;
        options.kobo = parser.value(koboOption);
        options.devicePath = parser.value(deviceOption);
        options.yes = parser.isSet(yesOption);
        options.unlocked = unlocked;
        options.hiddenOnly = parser.isSet(hiddenOption);
        options.visibleOnly = parser.isSet(visibleOption);
        options.origin = parser.value(originOption);
        options.search = parser.value(searchOption);
        return runKoboBooksCommand(&settings, &apiKeyStore, &network, arguments.mid(1), options);
    }

    if (command == QLatin1String("device")) {
        return runDeviceCommand(&settings, arguments.mid(1), parser.value(koboOption), parser.value(deviceOption), parser.isSet(allOption));
    }

    if (command.isEmpty()) {
        parser.showHelp(UsageError);
    }
    printError(i18n("unknown command \"%1\"; see kobold --help", command));
    return UsageError;
}
