// SPDX-License-Identifier: MIT

#include "apikeystore.h"

#include <KLocalizedString>
#include <qt6keychain/keychain.h>

#include <QHash>
#include <QMetaObject>

namespace Kobold
{

namespace
{
ApiKeyStore::Result resultFromJob(const QKeychain::Job *job)
{
    ApiKeyStore::Result result;
    switch (job->error()) {
    case QKeychain::NoError:
        result.status = ApiKeyStore::Result::Status::Ok;
        break;
    case QKeychain::EntryNotFound:
        result.status = ApiKeyStore::Result::Status::NotFound;
        break;
    default:
        result.status = ApiKeyStore::Result::Status::Error;
        result.errorString = i18n("keychain error: %1", job->errorString());
        break;
    }
    return result;
}

// KOBOLD_KEYCHAIN=memory: keys live in this process only, the system keychain is never read
// or written. Every test runs with it.
bool inMemory()
{
    static const bool memory = qEnvironmentVariable("KOBOLD_KEYCHAIN") == QLatin1String("memory");
    return memory;
}

QHash<QString, QString> &memoryKeys()
{
    static QHash<QString, QString> keys;
    return keys;
}

void later(QObject *context, std::function<void()> call)
{
    QMetaObject::invokeMethod(context, std::move(call), Qt::QueuedConnection);
}

template<typename JobType>
JobType *createJob(const QString &service, const QString &key, QObject *parent)
{
    auto *job = new JobType(service, parent);
    job->setKey(key);
    job->setAutoDelete(true);
    // Never store the secret in a plaintext QSettings file when no keychain is available.
    job->setInsecureFallback(false);
    return job;
}
}

ApiKeyStore::ApiKeyStore(QObject *parent)
    : ApiKeyStore(DefaultService, DefaultKey, parent)
{
}

ApiKeyStore::ApiKeyStore(const QString &service, const QString &key, QObject *parent)
    : QObject(parent)
    , m_service(service)
    , m_key(key)
{
}

void ApiKeyStore::read(std::function<void(const Result &)> callback)
{
    if (inMemory()) {
        const QString entry = m_service + QLatin1Char('/') + m_key;
        later(this, [entry, callback = std::move(callback)]() {
            Result result;
            result.status = memoryKeys().contains(entry) ? Result::Status::Ok : Result::Status::NotFound;
            result.apiKey = memoryKeys().value(entry);
            callback(result);
        });
        return;
    }
    auto *job = createJob<QKeychain::ReadPasswordJob>(m_service, m_key, this);
    connect(job, &QKeychain::Job::finished, this, [callback = std::move(callback)](QKeychain::Job *finishedJob) {
        Result result = resultFromJob(finishedJob);
        if (result.status == Result::Status::Ok) {
            result.apiKey = static_cast<QKeychain::ReadPasswordJob *>(finishedJob)->textData();
        }
        callback(result);
    });
    job->start();
}

void ApiKeyStore::write(const QString &apiKey, std::function<void(const Result &)> callback)
{
    if (inMemory()) {
        memoryKeys().insert(m_service + QLatin1Char('/') + m_key, apiKey);
        later(this, [callback = std::move(callback)]() {
            Result result;
            result.status = Result::Status::Ok;
            callback(result);
        });
        return;
    }
    auto *job = createJob<QKeychain::WritePasswordJob>(m_service, m_key, this);
    job->setTextData(apiKey);
    connect(job, &QKeychain::Job::finished, this, [callback = std::move(callback)](QKeychain::Job *finishedJob) {
        callback(resultFromJob(finishedJob));
    });
    job->start();
}

void ApiKeyStore::remove(std::function<void(const Result &)> callback)
{
    if (inMemory()) {
        memoryKeys().remove(m_service + QLatin1Char('/') + m_key);
        later(this, [callback = std::move(callback)]() {
            Result result;
            result.status = Result::Status::Ok;
            callback(result);
        });
        return;
    }
    auto *job = createJob<QKeychain::DeletePasswordJob>(m_service, m_key, this);
    connect(job, &QKeychain::Job::finished, this, [callback = std::move(callback)](QKeychain::Job *finishedJob) {
        callback(resultFromJob(finishedJob));
    });
    job->start();
}

} // namespace Kobold
