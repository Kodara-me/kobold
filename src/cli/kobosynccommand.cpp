// SPDX-License-Identifier: MIT

#include "kobosynccommand.h"

#include "console.h"
#include "exitcode.h"
#include "kobosyncrunner.h"

#include <KLocalizedString>

#include <QEventLoop>
#include <QTimer>

namespace Kobold::Cli
{

int runKoboSyncCommand(Settings *settings,
                       ApiKeyStore *apiKeyStore,
                       QNetworkAccessManager *network,
                       const QStringList &arguments,
                       const KoboSyncCommandOptions &options)
{
    static const QHash<QString, KoboSyncRunner::Action> actions{
        {QStringLiteral("status"), KoboSyncRunner::Action::Status},
        {QStringLiteral("install"), KoboSyncRunner::Action::Install},
        {QStringLiteral("uninstall"), KoboSyncRunner::Action::Uninstall},
        {QStringLiteral("reinstall"), KoboSyncRunner::Action::Reinstall},
        {QStringLiteral("set-key"), KoboSyncRunner::Action::SetKey},
        {QStringLiteral("reset"), KoboSyncRunner::Action::Reset},
    };
    const QString name = arguments.value(0);
    if (arguments.size() != 1 || !actions.contains(name)) {
        printError(i18n("usage: kobold kobo-sync status | install [--key-from-stdin] | uninstall [--delete-key] | reinstall | set-key [--create] "
                        "[--delete-old] | reset [--yes]"));
        return UsageError;
    }

    KoboSyncRunner::Options runOptions;
    runOptions.action = actions.value(name);
    runOptions.dryRun = options.dryRun;
    runOptions.kobo = options.kobo;
    runOptions.devicePath = options.devicePath;
    runOptions.createKey = options.create;
    runOptions.deleteOldKey = options.deleteOld;
    runOptions.deleteKey = options.deleteKey;

    const bool readsKey = (runOptions.action == KoboSyncRunner::Action::Install && options.keyFromStdin)
        || (runOptions.action == KoboSyncRunner::Action::SetKey && !options.create);
    if (readsKey && !options.dryRun) {
        runOptions.pastedKey = readSecret(i18n("Komga API key for the Kobo: "));
        if (runOptions.pastedKey.isEmpty()) {
            printError(i18n("no API key read from standard input"));
            return UsageError;
        }
    } else if (readsKey) {
        runOptions.pastedKey = QStringLiteral("****");
    }

    if (runOptions.action == KoboSyncRunner::Action::Reset && !options.yes && !options.dryRun) {
        if (!isInteractive()) {
            printError(i18n("kobo-sync reset asks for confirmation; pass --yes to skip it"));
            return UsageError;
        }
        const QString answer = readLine(i18n("Every book will be downloaded again by the Kobo. Continue? [y/N] "));
        if (!answer.startsWith(i18nc("@info first letter of the answer yes", "y"), Qt::CaseInsensitive)) {
            printLine(i18n("Nothing was changed."));
            return Success;
        }
    }

    KoboSyncRunner runner(settings, apiKeyStore, network);
    QObject::connect(&runner, &Runner::output, &runner, [](Runner::OutputKind kind, const QString &text) {
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
    // Komga refused to create a key with the API key: sign in once (§4.2).
    QObject::connect(&runner, &KoboSyncRunner::credentialsNeeded, &runner, [&runner]() {
        if (!isInteractive()) {
            printError(i18n("Komga does not let the API key create keys; run this in a terminal to sign in, or pass --key-from-stdin"));
            runner.cancelCredentials();
            return;
        }
        printLine(i18n("Komga asks for your email and password to create the Kobo's key. They are used once and not kept."));
        const QString email = readLine(i18n("Komga email: "));
        const QString password = readSecret(i18n("Komga password: "));
        if (email.isEmpty() || password.isEmpty()) {
            runner.cancelCredentials();
            return;
        }
        runner.provideCredentials(email, password);
    });

    QEventLoop loop;
    QObject::connect(&runner, &Runner::finished, &loop, [&loop](int code) {
        QTimer::singleShot(0, &loop, [&loop, code]() {
            loop.exit(code);
        });
    });
    QTimer::singleShot(0, &runner, [&runner, runOptions]() {
        runner.run(runOptions);
    });
    return loop.exec();
}

} // namespace Kobold::Cli
