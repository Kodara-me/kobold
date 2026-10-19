// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <functional>

class QNetworkAccessManager;
class QObject;

namespace Kobold
{
class ApiKeyStore;
class KomgaClient;
class Settings;
}

namespace Kobold
{

// Reads the API key, creates a KomgaClient (child of parent) and checks that Komga accepts
// the key. Calls exactly one of onReady or onError (with the exit code and message).
// Lets client answer shelf ids from the shelf database (SPEC-SHELF.md §6). Returns false when
// there is no shelf. The database lives as long as the client.
bool attachShelves(KomgaClient *client);

// Without a Komga API key, a session still opens when shelves exist: the client then only
// answers shelf ids (SPEC-SHELF.md: Komga stays optional).
void openKomgaSession(Settings *settings,
                      ApiKeyStore *apiKeyStore,
                      QNetworkAccessManager *network,
                      QObject *parent,
                      std::function<void(KomgaClient *client)> onReady,
                      std::function<void(int exitCode, const QString &message)> onError);

} // namespace Kobold
