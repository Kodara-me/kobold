// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>

#include <functional>

namespace Kobold
{

// Komga API key in the system keychain (Secret Service / KWallet) through QtKeychain
// (SPEC.md §3). The key is never written to a file: the plaintext fallback of QtKeychain
// is disabled.
class ApiKeyStore : public QObject
{
    Q_OBJECT

public:
    static inline const QString DefaultService = QStringLiteral("kobold");
    static inline const QString DefaultKey = QStringLiteral("komga-api-key");

    // Keychain entry of the ComicVine API key (SPEC-SHELF.md §4.3).
    static inline const QString ComicVineKey = QStringLiteral("comicvine-api-key");

    // Keychain entry of the Komga Kobo sync key of a Kobo (SPEC-KOBO-SYNC.md §4.1).
    static QString koboSyncKey(const QString &serial)
    {
        return QStringLiteral("kobo-sync-key/") + serial;
    }

    struct Result {
        enum class Status {
            Ok,
            // No key stored yet. The keychain itself is reachable.
            NotFound,
            // Keychain unavailable, access denied, or other failure.
            Error,
        };

        Status status = Status::Error;
        // The key, for a successful read.
        QString apiKey;
        QString errorString;
    };

    explicit ApiKeyStore(QObject *parent = nullptr);
    ApiKeyStore(const QString &service, const QString &key, QObject *parent = nullptr);

    // Virtual so that tests can keep keys in memory instead of the real keychain.
    virtual void read(std::function<void(const Result &)> callback);
    virtual void write(const QString &apiKey, std::function<void(const Result &)> callback);
    virtual void remove(std::function<void(const Result &)> callback);

private:
    QString m_service;
    QString m_key;
};

} // namespace Kobold
