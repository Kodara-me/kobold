// SPDX-License-Identifier: MIT

#include "parentalcommand.h"

#include "apikeystore.h"
#include "console.h"
#include "exitcode.h"
#include "parentallock.h"
#include "settings.h"

#include <KLocalizedString>

#include <QEventLoop>
#include <QTimer>

namespace Kobold::Cli
{

namespace
{
const QString Usage = QStringLiteral(
    "usage: kobold parental status | enable | disable [--reset-pin] | set-age <age> | add-keyword <word> | remove-keyword <word>");

// Runs one asynchronous lock operation to its end.
std::pair<bool, QString> wait(const std::function<void(ParentalLock::Callback)> &call)
{
    QEventLoop loop;
    std::pair<bool, QString> outcome{false, QString()};
    QTimer::singleShot(0, &loop, [&]() {
        call([&](bool ok, const QString &message) {
            outcome = {ok, message};
            loop.quit();
        });
    });
    loop.exec();
    return outcome;
}

QString askPin(const QString &prompt)
{
    return readSecret(prompt);
}

void printStatus(const ParentalLock &lock)
{
    const ParentalRules rules = lock.rules();
    if (!rules.enabled) {
        printLine(i18n("Parental control: off"));
        return;
    }
    printLine(i18n("Parental control: on"));
    printLine(rules.ageLimit > 0 ? i18n("  Age rating from: %1", rules.ageLimit) : i18n("  Age rule: off"));
    printLine(rules.keywords.isEmpty() ? i18n("  Genre rule: off") : i18n("  Genres containing: %1", rules.keywords.join(QLatin1String(", "))));
}
}

bool unlockParental(Settings *settings)
{
    if (!settings->parentalRules().enabled) {
        return true;
    }
    if (!isInteractive()) {
        printError(i18n("--unlock needs a terminal to type the parental PIN"));
        return false;
    }
    ApiKeyStore pinStore(ApiKeyStore::DefaultService, ParentalLock::PinEntry);
    ParentalLock lock(settings, &pinStore);
    const QString pin = askPin(i18n("Parental PIN: "));
    const auto [ok, message] = wait([&](ParentalLock::Callback done) {
        lock.unlock(pin, done);
    });
    if (!ok) {
        printError(message);
    }
    return ok;
}

int runParentalCommand(Settings *settings, const QStringList &arguments, bool resetPin)
{
    const QString action = arguments.value(0);
    ApiKeyStore pinStore(ApiKeyStore::DefaultService, ParentalLock::PinEntry);
    ParentalLock lock(settings, &pinStore);

    if (action == QLatin1String("status") && arguments.size() == 1) {
        printStatus(lock);
        return Success;
    }

    const bool known = (action == QLatin1String("enable") && arguments.size() == 1) || (action == QLatin1String("disable") && arguments.size() == 1)
        || (arguments.size() == 2
            && (action == QLatin1String("set-age") || action == QLatin1String("add-keyword") || action == QLatin1String("remove-keyword")));
    if (!known) {
        printError(Usage);
        return UsageError;
    }
    if (!isInteractive()) {
        printError(i18n("kobold parental %1 needs a terminal to type the PIN", action));
        return UsageError;
    }

    if (action == QLatin1String("enable")) {
        if (lock.enabled()) {
            printLine(i18n("Parental control is already on."));
            return Success;
        }
        const QString pin = askPin(i18n("New parental PIN (4 to 8 digits): "));
        if (askPin(i18n("New parental PIN again: ")) != pin) {
            printError(i18n("the two PINs differ"));
            return UsageError;
        }
        const auto [ok, message] = wait([&](ParentalLock::Callback done) {
            lock.enable(pin, done);
        });
        if (!ok) {
            printError(message);
            return UsageError;
        }
        printLine(i18n("Parental control is on; adult series are hidden until the PIN is given."));
        printStatus(lock);
        return Success;
    }

    if (!lock.enabled()) {
        printError(i18n("parental control is off; turn it on with: kobold parental enable"));
        return UsageError;
    }

    if (action == QLatin1String("disable")) {
        if (resetPin) {
            const QString answer = readLine(i18n("Forget the parental PIN and turn parental control off? [y/N] "));
            if (!answer.startsWith(i18nc("@info first letter of the answer yes", "y"), Qt::CaseInsensitive)) {
                printLine(i18n("Nothing was changed."));
                return Success;
            }
            wait([&](ParentalLock::Callback done) {
                lock.resetPin(done);
            });
            printLine(i18n("Parental control is off."));
            return Success;
        }
        const QString pin = askPin(i18n("Parental PIN: "));
        const auto [ok, message] = wait([&](ParentalLock::Callback done) {
            lock.disable(pin, done);
        });
        if (!ok) {
            printError(message);
            return UsageError;
        }
        printLine(i18n("Parental control is off."));
        return Success;
    }

    // Rules: the PIN opens the lock for this change.
    const QString pin = askPin(i18n("Parental PIN: "));
    const auto [ok, message] = wait([&](ParentalLock::Callback done) {
        lock.unlock(pin, done);
    });
    if (!ok) {
        printError(message);
        return UsageError;
    }
    ParentalRules rules = lock.rules();
    const QString value = arguments.at(1);
    if (action == QLatin1String("set-age")) {
        bool number = false;
        rules.ageLimit = value.toInt(&number);
        if (!number) {
            printError(Usage);
            return UsageError;
        }
    } else if (action == QLatin1String("add-keyword")) {
        rules.keywords.append(value);
    } else {
        rules.keywords.removeIf([&value](const QString &keyword) {
            return keyword.compare(value, Qt::CaseInsensitive) == 0;
        });
    }
    QString error;
    if (!lock.setRules(rules.ageLimit, rules.keywords, &error)) {
        printError(error);
        return UsageError;
    }
    printStatus(lock);
    return Success;
}

} // namespace Kobold::Cli
