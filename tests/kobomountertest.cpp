// SPDX-License-Identifier: MIT

#include "kobomounter.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;

namespace
{
// Volumes in memory: mounting makes the Kobo's folder appear.
class FakeBackend : public VolumeBackend
{
public:
    QList<KoboVolume> list;
    QString mountRoot;
    bool busy = false;
    bool powerOffWorks = true;
    QStringList calls;

    QList<KoboVolume> volumes() const override
    {
        return list;
    }

    void mount(const QString &udi, Done done) override
    {
        calls.append(QStringLiteral("mount ") + udi);
        for (KoboVolume &volume : list) {
            if (volume.udi == udi) {
                volume.mounted = true;
                volume.mountPoint = mountRoot;
            }
        }
        done(true, QString());
    }

    void unmount(const QString &udi, Done done) override
    {
        calls.append(QStringLiteral("unmount ") + udi);
        if (busy) {
            done(false, QStringLiteral("busy"));
            return;
        }
        for (KoboVolume &volume : list) {
            if (volume.udi == udi) {
                volume.mounted = false;
            }
        }
        done(true, QString());
    }

    void powerOff(const QString &udi, Done done) override
    {
        calls.append(QStringLiteral("power off ") + udi);
        done(powerOffWorks, powerOffWorks ? QString() : QStringLiteral("cannot power off"));
    }
};
}

class KoboMounterTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<Settings> m_settings;
    FakeBackend m_backend;
    QString m_kobo;

    std::pair<bool, QString> run(const std::function<void(KoboMounter::Done)> &call)
    {
        std::pair<bool, QString> result;
        call([&result](bool ok, const QString &message) {
            result = {ok, message};
        });
        return result;
    }

private Q_SLOTS:
    void init()
    {
        m_kobo = m_dir.filePath(QStringLiteral("KOBOeReader"));
        QDir().mkpath(m_kobo + QStringLiteral("/.kobo"));
        QFile version(m_kobo + QStringLiteral("/.kobo/version"));
        QVERIFY(version.open(QIODevice::WriteOnly));
        version.write("N1,1,2,3,4,00000000-0000-0000-0000-000000000386\n");
        version.close();
        QFile::remove(m_dir.filePath(QStringLiteral("koboldrc")));
        m_settings = std::make_unique<Settings>(KSharedConfig::openConfig(m_dir.filePath(QStringLiteral("koboldrc")), KConfig::SimpleConfig));
        m_backend = FakeBackend();
        m_backend.mountRoot = m_kobo;
        m_backend.list = {KoboVolume{QStringLiteral("/org/freedesktop/UDisks2/block_devices/sdb"), QStringLiteral("AAAA-1111"), false, QString()}};
    }

    // MAC 1: a plugged Kobo shows as not mounted; mounting registers it and its UUID.
    void mountsAndRecognizes()
    {
        KoboMounter mounter(m_settings.get(), &m_backend);
        QCOMPARE(mounter.unmounted().size(), 1);
        QVERIFY(!mounter.unmounted().first().kobo);

        const auto [ok, mountPoint] = run([&](KoboMounter::Done done) {
            mounter.mount(m_backend.list.first().udi, done);
        });
        QVERIFY(ok);
        QCOMPARE(mountPoint, m_kobo);
        QCOMPARE(m_settings->koboVolumeUuid(QStringLiteral("N1")), QStringLiteral("AAAA-1111"));
        QVERIFY(mounter.unmounted().isEmpty());

        // Unmounted again: recognized by its UUID, under its name.
        m_backend.list.first().mounted = false;
        QCOMPARE(mounter.unmounted().first().kobo->serial, QStringLiteral("N1"));
        QCOMPARE(mounter.unmounted().first().displayName(), QStringLiteral("Clara 2E"));
    }

    // MAC 2: eject unmounts then powers off.
    void ejectUnmountsThenPowersOff()
    {
        m_backend.list.first().mounted = true;
        m_backend.list.first().mountPoint = m_kobo;
        KoboMounter mounter(m_settings.get(), &m_backend);
        const auto [ok, message] = run([&](KoboMounter::Done done) {
            mounter.eject(m_kobo, done);
        });
        QVERIFY(ok);
        QVERIFY(message.contains(QLatin1String("unplugged")));
        QCOMPARE(m_backend.calls, (QStringList{QStringLiteral("unmount /org/freedesktop/UDisks2/block_devices/sdb"),
                                               QStringLiteral("power off /org/freedesktop/UDisks2/block_devices/sdb")}));
        QCOMPARE(m_settings->koboVolumeUuid(QStringLiteral("N1")), QStringLiteral("AAAA-1111"));
    }

    // MAC 3: a busy Kobo is not forced.
    void busyKoboIsNotForced()
    {
        m_backend.list.first().mounted = true;
        m_backend.list.first().mountPoint = m_kobo;
        m_backend.busy = true;
        KoboMounter mounter(m_settings.get(), &m_backend);
        const auto [ok, message] = run([&](KoboMounter::Done done) {
            mounter.eject(m_kobo, done);
        });
        QVERIFY(!ok);
        QCOMPARE(message, QStringLiteral("busy"));
        QVERIFY(!m_backend.calls.contains(QStringLiteral("power off /org/freedesktop/UDisks2/block_devices/sdb")));
    }

    void powerOffRefusedStillUnmounts()
    {
        m_backend.list.first().mounted = true;
        m_backend.list.first().mountPoint = m_kobo;
        m_backend.powerOffWorks = false;
        KoboMounter mounter(m_settings.get(), &m_backend);
        const auto [ok, message] = run([&](KoboMounter::Done done) {
            mounter.eject(m_kobo, done);
        });
        QVERIFY(ok);
        QCOMPARE(message, QStringLiteral("cannot power off"));
    }

    void ejectWithoutKobo()
    {
        KoboMounter mounter(m_settings.get(), &m_backend);
        const auto [ok, message] = run([&](KoboMounter::Done done) {
            mounter.eject(m_dir.filePath(QStringLiteral("elsewhere")), done);
        });
        QVERIFY(!ok);
    }
};

QTEST_GUILESS_MAIN(KoboMounterTest)

#include "kobomountertest.moc"
