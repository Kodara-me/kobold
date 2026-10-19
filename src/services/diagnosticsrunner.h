// SPDX-License-Identifier: MIT

#pragma once

#include "runner.h"

#include <QObject>
#include <QString>

#include "comicsetup.h"

class QNetworkAccessManager;

namespace Kobold
{
class ApiKeyStore;
class Converter;
class KomgaClient;
class Settings;
}

namespace Kobold
{

// `kobold doctor` (SPEC-INSTALL.md §7.1) and `kobold status` (SPEC.md §12).
// Both run the same checks; status also reports the selection, the managed books and the
// pending sync plan.
class DiagnosticsRunner : public Runner
{
    Q_OBJECT

public:
    enum class Mode {
        Doctor,
        Status,
    };

    DiagnosticsRunner(Mode mode, Settings *settings, ApiKeyStore *apiKeyStore, QNetworkAccessManager *network, QObject *parent = nullptr);

    // Emits finished() exactly once.
    // The parental PIN was given: name adult series (SPEC-PARENTAL.md §4).
    void setUnlocked(bool unlocked)
    {
        m_unlocked = unlocked;
    }

    void run(const QString &devicePath, const QString &kobo = QString());

Q_SIGNALS:
    // Each check as it completes; level: 0 ok, 1 warning, 2 error.
    void check(int level, const QString &message, const QString &hint);

private:
    enum class Level {
        Ok,
        Warning,
        Error,
    };

    void report(Level level, const QString &message, const QString &hint = QString(), int exitCode = 0);
    void checkConverter();
    void checkSqlite();
    void checkKeychain();
    void checkKomga(const QString &apiKey);
    void checkDevice();
    void checkComics();
    void finishChecks();
    void reportStatus();
    void reportDevicePlan(const QStringList &selection);
    void done();

    Mode m_mode;
    Settings *m_settings;
    bool m_unlocked = false;
    ApiKeyStore *m_apiKeyStore;
    QNetworkAccessManager *m_network;
    Converter *m_converter = nullptr;
    KomgaClient *m_client = nullptr;
    QString m_devicePath;
    QString m_kobo;
    QString m_deviceRoot;
    ComicSetup m_comics;
    int m_exitCode = 0;
};

} // namespace Kobold
