// SPDX-License-Identifier: MIT

#include "memorykeystore.h"
#include "parentallock.h"
#include "settings.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace Kobold::Testing;

class ParentalLockTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QHash<QString, QString> m_keys;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<MemoryKeyStore> m_store;
    std::unique_ptr<ParentalLock> m_lock;

    std::pair<bool, QString> wait(const std::function<void(ParentalLock::Callback)> &call)
    {
        std::optional<std::pair<bool, QString>> result;
        call([&result](bool ok, const QString &message) {
            result = {ok, message};
        });
        if (!QTest::qWaitFor([&result]() {
                return result.has_value();
            })) {
            return {false, QStringLiteral("timeout")};
        }
        return *result;
    }

private Q_SLOTS:
    void init()
    {
        m_keys.clear();
        QFile::remove(m_dir.filePath(QStringLiteral("koboldrc")));
        m_settings = std::make_unique<Settings>(KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig));
        m_store = std::make_unique<MemoryKeyStore>(&m_keys, ParentalLock::PinEntry);
        m_lock = std::make_unique<ParentalLock>(m_settings.get(), m_store.get());
    }

    void cleanup()
    {
        m_lock.reset();
        m_store.reset();
        m_settings.reset();
    }

    void offByDefault()
    {
        QVERIFY(!m_lock->enabled());
        QVERIFY(!m_lock->hidesAdult());
    }

    // PAC 4, 6.
    void enableUnlockLock()
    {
        QVERIFY(!wait([this](auto cb) { m_lock->enable(QStringLiteral("12"), cb); }).first);
        QVERIFY(wait([this](auto cb) { m_lock->enable(QStringLiteral("2468"), cb); }).first);
        QVERIFY(m_lock->enabled());
        QVERIFY(m_lock->hidesAdult());
        QVERIFY(m_keys.value(ParentalLock::PinEntry).startsWith(QLatin1String("pbkdf2-sha256$")));
        QVERIFY(!m_keys.value(ParentalLock::PinEntry).contains(QLatin1String("2468")));

        QSignalSpy changed(m_lock.get(), &ParentalLock::changed);
        QVERIFY(!wait([this](auto cb) { m_lock->unlock(QStringLiteral("1111"), cb); }).first);
        QVERIFY(m_lock->hidesAdult());
        QVERIFY(wait([this](auto cb) { m_lock->unlock(QStringLiteral("2468"), cb); }).first);
        QVERIFY(!m_lock->hidesAdult());
        m_lock->lock();
        QVERIFY(m_lock->hidesAdult());
        QCOMPARE(changed.count(), 2);
    }

    // PAC 4: three wrong PINs, then even the right one waits.
    void wrongPinsDelay()
    {
        QVERIFY(wait([this](auto cb) { m_lock->enable(QStringLiteral("2468"), cb); }).first);
        for (int i = 0; i < ParentalLock::MaxWrongPins; ++i) {
            QVERIFY(!wait([this](auto cb) { m_lock->unlock(QStringLiteral("0000"), cb); }).first);
        }
        QVERIFY(m_lock->secondsBeforeRetry() > 0);
        const auto [ok, message] = wait([this](auto cb) { m_lock->unlock(QStringLiteral("2468"), cb); });
        QVERIFY(!ok);
        QVERIFY(message.contains(QLatin1String("Too many")));
    }

    // PAC 7.
    void rulesNeedTheLockOpen()
    {
        QVERIFY(wait([this](auto cb) { m_lock->enable(QStringLiteral("2468"), cb); }).first);
        QString error;
        QVERIFY(!m_lock->setRules(16, {}, &error));
        QVERIFY(wait([this](auto cb) { m_lock->unlock(QStringLiteral("2468"), cb); }).first);
        QVERIFY(m_lock->setRules(16, {QStringLiteral("hentai"), QStringLiteral(" HENTAI ")}, &error));
        QCOMPARE(m_lock->rules().ageLimit, 16);
        QCOMPARE(m_lock->rules().keywords, QStringList{QStringLiteral("hentai")});
        QVERIFY(!m_lock->setRules(30, {}, &error));
    }

    void disableNeedsThePin()
    {
        QVERIFY(wait([this](auto cb) { m_lock->enable(QStringLiteral("2468"), cb); }).first);
        QVERIFY(!wait([this](auto cb) { m_lock->disable(QStringLiteral("1357"), cb); }).first);
        QVERIFY(m_lock->enabled());
        QVERIFY(wait([this](auto cb) { m_lock->disable(QStringLiteral("2468"), cb); }).first);
        QVERIFY(!m_lock->enabled());
        QVERIFY(!m_keys.contains(ParentalLock::PinEntry));
    }

    void inactivityLocks()
    {
        QVERIFY(wait([this](auto cb) { m_lock->enable(QStringLiteral("2468"), cb); }).first);
        m_settings->setParentalAutoLockMinutes(0);
        QVERIFY(wait([this](auto cb) { m_lock->unlock(QStringLiteral("2468"), cb); }).first);
        QVERIFY(!m_lock->hidesAdult());
        // 0 means only when quitting: still unlocked later.
        QTest::qWait(50);
        QVERIFY(!m_lock->hidesAdult());
    }
};

QTEST_GUILESS_MAIN(ParentalLockTest)

#include "parentallocktest.moc"
