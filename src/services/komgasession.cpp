// SPDX-License-Identifier: MIT

#include "komgasession.h"
#include "exitcode.h"

#include "apikeystore.h"
#include "komgaclient.h"
#include "settings.h"
#include "shelfdatabase.h"
#include "shelflibrary.h"

#include <KLocalizedString>

#include <QFileInfo>

#include <memory>

namespace Kobold
{

namespace
{
// Owns the shelf database and its book source for a client.
class ShelfAttachment : public QObject
{
public:
    ShelfAttachment(std::unique_ptr<ShelfDatabase> database, QObject *parent)
        : QObject(parent)
        , m_database(std::move(database))
        , m_library(m_database.get())
    {
    }

    ShelfLibrary *library()
    {
        return &m_library;
    }

private:
    std::unique_ptr<ShelfDatabase> m_database;
    ShelfLibrary m_library;
};
}

bool attachShelves(KomgaClient *client)
{
    if (!QFileInfo::exists(ShelfDatabase::defaultFilePath())) {
        return false;
    }
    auto database = std::make_unique<ShelfDatabase>();
    QString error;
    if (!database->open(&error) || database->shelves().isEmpty()) {
        return false;
    }
    auto *attachment = new ShelfAttachment(std::move(database), client);
    client->setBookSource(attachment->library());
    return true;
}

void openKomgaSession(Settings *settings,
                      ApiKeyStore *apiKeyStore,
                      QNetworkAccessManager *network,
                      QObject *parent,
                      std::function<void(KomgaClient *client)> onReady,
                      std::function<void(int exitCode, const QString &message)> onError)
{
    apiKeyStore->read([=](const ApiKeyStore::Result &key) {
        switch (key.status) {
        case ApiKeyStore::Result::Status::NotFound: {
            // Shelves only: no Komga needed.
            auto *client = new KomgaClient(network, parent);
            if (attachShelves(client)) {
                onReady(client);
                return;
            }
            client->deleteLater();
            onError(UsageError, i18n("no Komga API key; run: kobold config set-api-key"));
            return;
        }
        case ApiKeyStore::Result::Status::Error:
            onError(UsageError, key.errorString);
            return;
        case ApiKeyStore::Result::Status::Ok:
            break;
        }

        auto *client = new KomgaClient(network, parent);
        client->setBaseUrl(settings->komgaUrl());
        client->setApiKey(key.apiKey);
        attachShelves(client);
        client->fetchCurrentUser([=](const KomgaResult<KomgaUser> &user) {
            if (!user.ok()) {
                onError(KomgaFailure, user.error.message);
                return;
            }
            onReady(client);
        });
    });
}

} // namespace Kobold
