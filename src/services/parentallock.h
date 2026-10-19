// SPDX-License-Identifier: MIT

#pragma once

#include "parental.h"

#include <QDeadlineTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

namespace Kobold
{
class ApiKeyStore;
class Settings;

// Locked or unlocked view of adult content (SPEC-PARENTAL.md §3, §4). The PIN hash lives in
// the keychain; the lock state lives only in memory, so every start is locked.
class ParentalLock : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxWrongPins = 3;
    static constexpr int WrongPinDelaySeconds = 30;

    static inline const QString PinEntry = QStringLiteral("parental-pin");

    // pinStore keeps the PIN hash (the keychain entry PinEntry outside tests).
    ParentalLock(Settings *settings, ApiKeyStore *pinStore, QObject *parent = nullptr);

    bool enabled() const;
    ParentalRules rules() const;
    // True when adult content must be hidden: enabled and not unlocked.
    bool hidesAdult() const;
    bool isAdult(const KomgaSeries &series) const;

    // Seconds before a PIN may be tried again after too many wrong ones, 0 when allowed.
    int secondsBeforeRetry() const;

    using Callback = std::function<void(bool ok, const QString &message)>;

    void unlock(const QString &pin, Callback callback);
    void lock();
    // Counts as activity for the inactivity lock.
    void touch();

    // Turns parental control on with a new PIN (already typed twice by the caller).
    void enable(const QString &newPin, Callback callback);
    // Needs the PIN.
    void disable(const QString &pin, Callback callback);
    // Turns it off and forgets the PIN, for a forgotten PIN (confirmed by the caller).
    void resetPin(Callback callback);
    // Needs the lock to be open.
    bool setRules(int ageLimit, const QStringList &keywords, QString *errorString);
    void setAutoLockMinutes(int minutes);

Q_SIGNALS:
    // Enabled, rules or lock state changed: what is hidden may differ.
    void changed();

private:
    void checkPin(const QString &pin, std::function<void(bool ok, const QString &message)> callback);
    void restartInactivityTimer();

    Settings *m_settings;
    ApiKeyStore *m_pinStore;
    bool m_unlocked = false;
    int m_wrongPins = 0;
    QDeadlineTimer m_retryAfter;
    QTimer m_inactivity;
};

} // namespace Kobold
