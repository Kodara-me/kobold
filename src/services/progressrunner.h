// SPDX-License-Identifier: MIT

#pragma once

#include "parental.h"
#include "runner.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace Kobold
{
class ApiKeyStore;
class Settings;
struct ProgressReport;
}

namespace Kobold
{

// `kobold pull-progress [--dry-run] [--device <path>]` (SPEC.md §11).
class ProgressRunner : public Runner
{
    Q_OBJECT

public:
    struct Options {
        bool dryRun = false;
        // Empty: detect the Kobo.
        QString devicePath;
        // --kobo: name or serial of a connected Kobo.
        QString kobo;
        // Books and series whose names the output leaves out (SPEC-PARENTAL.md §4).
        HiddenContent hidden;
        // Find the adult series to leave out from the settings (the command line, unless
        // unlocked); the application passes hidden instead.
        bool hideAdult = false;
    };

    ProgressRunner(Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent = nullptr);

    // Emits finished() exactly once.
    void run(const Options &options);

private:
    void report(const ProgressReport &report);
    void fail(int exitCode, const QString &message);

    Settings *m_settings;
    ApiKeyStore *m_apiKeyStore;
    QNetworkAccessManager *m_network;
    Options m_options;
    QString m_kobo;
};

} // namespace Kobold
