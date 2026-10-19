// SPDX-License-Identifier: MIT

#pragma once

#include "apikeystore.h"

#include <QHash>
#include <QTimer>

namespace Kobold::Testing
{

// Keeps keys in memory: the tests never touch the real keychain.
class MemoryKeyStore : public ApiKeyStore
{
public:
    MemoryKeyStore(QHash<QString, QString> *keys, const QString &entry, QObject *parent = nullptr)
        : ApiKeyStore(QStringLiteral("unused"), entry, parent)
        , m_keys(keys)
        , m_entry(entry)
    {
    }

    void read(std::function<void(const Result &)> callback) override
    {
        Result result;
        result.status = m_keys->contains(m_entry) ? Result::Status::Ok : Result::Status::NotFound;
        result.apiKey = m_keys->value(m_entry);
        QTimer::singleShot(0, this, [result, callback]() {
            callback(result);
        });
    }

    void write(const QString &apiKey, std::function<void(const Result &)> callback) override
    {
        m_keys->insert(m_entry, apiKey);
        QTimer::singleShot(0, this, [callback]() {
            callback(Result{Result::Status::Ok, {}, {}});
        });
    }

    void remove(std::function<void(const Result &)> callback) override
    {
        m_keys->remove(m_entry);
        QTimer::singleShot(0, this, [callback]() {
            callback(Result{Result::Status::Ok, {}, {}});
        });
    }

private:
    QHash<QString, QString> *m_keys;
    QString m_entry;
};

} // namespace Kobold::Testing
