// SPDX-License-Identifier: MIT

#include "parentallock.h"

#include "apikeystore.h"
#include "settings.h"

#include <KLocalizedString>

#include <chrono>

namespace Kobold
{

ParentalLock::ParentalLock(Settings *settings, ApiKeyStore *pinStore, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_pinStore(pinStore)
    , m_retryAfter(0)
{
    m_inactivity.setSingleShot(true);
    connect(&m_inactivity, &QTimer::timeout, this, &ParentalLock::lock);
}

bool ParentalLock::enabled() const
{
    return m_settings->parentalRules().enabled;
}

ParentalRules ParentalLock::rules() const
{
    return m_settings->parentalRules();
}

bool ParentalLock::hidesAdult() const
{
    return enabled() && !m_unlocked;
}

bool ParentalLock::isAdult(const KomgaSeries &series) const
{
    return m_settings->parentalRules().isAdult(series);
}

int ParentalLock::secondsBeforeRetry() const
{
    if (m_retryAfter.hasExpired()) {
        return 0;
    }
    return int(std::chrono::ceil<std::chrono::seconds>(m_retryAfter.remainingTimeAsDuration()).count());
}

void ParentalLock::checkPin(const QString &pin, std::function<void(bool ok, const QString &message)> callback)
{
    if (const int wait = secondsBeforeRetry(); wait > 0) {
        callback(false, i18np("Too many wrong PINs; try again in 1 second.", "Too many wrong PINs; try again in %1 seconds.", wait));
        return;
    }
    m_pinStore->read([this, pin, callback = std::move(callback)](const ApiKeyStore::Result &result) {
        if (result.status != ApiKeyStore::Result::Status::Ok) {
            callback(false,
                     result.status == ApiKeyStore::Result::Status::NotFound ? i18n("No parental PIN is set.")
                                                                             : i18n("Cannot read the parental PIN: %1", result.errorString));
            return;
        }
        if (!ParentalPin::verify(pin, result.apiKey)) {
            if (++m_wrongPins >= MaxWrongPins) {
                m_wrongPins = 0;
                m_retryAfter.setRemainingTime(std::chrono::seconds(WrongPinDelaySeconds));
            }
            callback(false, i18n("Wrong PIN."));
            return;
        }
        m_wrongPins = 0;
        callback(true, QString());
    });
}

void ParentalLock::unlock(const QString &pin, Callback callback)
{
    checkPin(pin, [this, callback = std::move(callback)](bool ok, const QString &message) {
        if (ok) {
            m_unlocked = true;
            restartInactivityTimer();
            Q_EMIT changed();
        }
        callback(ok, message);
    });
}

void ParentalLock::lock()
{
    m_inactivity.stop();
    if (m_unlocked) {
        m_unlocked = false;
        Q_EMIT changed();
    }
}

void ParentalLock::touch()
{
    if (m_unlocked) {
        restartInactivityTimer();
    }
}

void ParentalLock::restartInactivityTimer()
{
    const int minutes = m_settings->parentalAutoLockMinutes();
    if (minutes > 0) {
        m_inactivity.start(std::chrono::minutes(minutes));
    } else {
        m_inactivity.stop();
    }
}

void ParentalLock::enable(const QString &newPin, Callback callback)
{
    if (!ParentalPin::isValid(newPin)) {
        callback(false, i18n("The PIN must have 4 to 8 digits."));
        return;
    }
    m_pinStore->write(ParentalPin::hash(newPin), [this, callback = std::move(callback)](const ApiKeyStore::Result &result) {
        if (result.status != ApiKeyStore::Result::Status::Ok) {
            callback(false, i18n("Cannot store the parental PIN: %1", result.errorString));
            return;
        }
        ParentalRules rules = m_settings->parentalRules();
        rules.enabled = true;
        m_settings->setParentalRules(rules);
        m_settings->sync();
        m_unlocked = false;
        Q_EMIT changed();
        callback(true, QString());
    });
}

void ParentalLock::disable(const QString &pin, Callback callback)
{
    checkPin(pin, [this, callback = std::move(callback)](bool ok, const QString &message) {
        if (!ok) {
            callback(false, message);
            return;
        }
        resetPin(callback);
    });
}

void ParentalLock::resetPin(Callback callback)
{
    m_pinStore->remove([this, callback = std::move(callback)](const ApiKeyStore::Result &) {
        ParentalRules rules = m_settings->parentalRules();
        rules.enabled = false;
        m_settings->setParentalRules(rules);
        m_settings->sync();
        m_unlocked = false;
        m_inactivity.stop();
        Q_EMIT changed();
        callback(true, QString());
    });
}

bool ParentalLock::setRules(int ageLimit, const QStringList &keywords, QString *errorString)
{
    if (hidesAdult()) {
        *errorString = i18n("Unlock with the PIN first.");
        return false;
    }
    if (ageLimit < 0 || ageLimit > 21) {
        *errorString = i18n("The age must be between 1 and 21, or 0 to turn the age rule off.");
        return false;
    }
    ParentalRules rules = m_settings->parentalRules();
    rules.ageLimit = ageLimit;
    rules.keywords.clear();
    for (const QString &keyword : keywords) {
        if (!keyword.trimmed().isEmpty() && !rules.keywords.contains(keyword.trimmed(), Qt::CaseInsensitive)) {
            rules.keywords.append(keyword.trimmed());
        }
    }
    m_settings->setParentalRules(rules);
    m_settings->sync();
    Q_EMIT changed();
    return true;
}

void ParentalLock::setAutoLockMinutes(int minutes)
{
    m_settings->setParentalAutoLockMinutes(std::max(0, minutes));
    m_settings->sync();
    touch();
}

} // namespace Kobold
