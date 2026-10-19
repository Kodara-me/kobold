// SPDX-License-Identifier: MIT

#pragma once

#include "parental.h"

#include <KSharedConfig>

#include <QDateTime>
#include <QList>
#include <QString>
#include <QUrl>

#include <chrono>
#include <optional>

namespace Kobold
{

struct DeviceIdentity;

// A Kobo Kobold has seen (SPEC-DEVICES.md §2).
struct KnownKobo {
    QString serial;
    QString name;
    int productId = 0;
    QDateTime lastSeen;
};

// Persistent settings in ~/.config/koboldrc (SPEC.md §3). Never holds the API key.
class Settings
{
public:
    static inline const QString DefaultKomgaUrl = QStringLiteral("http://localhost:25600");
    static inline const QString DefaultBooksFolder = QStringLiteral("kobold");
    static constexpr std::chrono::seconds DefaultConversionTimeout{300};
    static constexpr std::chrono::seconds DefaultComicConversionTimeout{1800};

    // Uses koboldrc in the user configuration directory.
    Settings();
    explicit Settings(KSharedConfigPtr config);

    QUrl komgaUrl() const;
    // Accepts an http(s) URL with a host and no credentials, query or fragment.
    // Stores it without trailing slash. Returns false and sets errorString otherwise.
    bool setKomgaUrl(const QString &url, QString *errorString);

    QString booksFolder() const;
    std::chrono::seconds conversionTimeout() const;
    std::chrono::seconds comicConversionTimeout() const;

    // KCC profile chosen for the Kobo with this serial number (SPEC-COMICS.md §4.3),
    // or empty when none is set.
    QString comicProfileOverride(const QString &serial) const;
    void setComicProfileOverride(const QString &serial, const QString &profile);
    void clearComicProfileOverride(const QString &serial);

    // Komga Kobo sync of a Kobo (SPEC-KOBO-SYNC.md §4.1). The key itself is in the keychain.
    QString koboSyncKeyId(const QString &serial) const;
    void setKoboSyncKeyId(const QString &serial, const QString &keyId);
    bool koboSyncInstalled(const QString &serial) const;
    void setKoboSyncInstalled(const QString &serial, bool installed);
    // The api_endpoint found before the first install; empty when there was none.
    QString koboSyncOriginalEndpoint(const QString &serial) const;
    void setKoboSyncOriginalEndpoint(const QString &serial, const QString &endpoint);

    // Parental control (SPEC-PARENTAL.md §3); the PIN itself is in the keychain.
    ParentalRules parentalRules() const;
    void setParentalRules(const ParentalRules &rules);
    int parentalAutoLockMinutes() const;
    void setParentalAutoLockMinutes(int minutes);

    // File system UUID of a Kobo's volume, to recognize it before mounting (SPEC-MOUNT.md §2).
    QString koboVolumeUuid(const QString &serial) const;
    void setKoboVolumeUuid(const QString &serial, const QString &uuid);
    std::optional<KnownKobo> findKoboByVolumeUuid(const QString &uuid) const;
    // Back up a Kobo's database before each sync (SPEC-KOBO-BOOKS.md §4).
    bool backupBeforeSync(const QString &serial) const;
    void setBackupBeforeSync(const QString &serial, bool backup);
    // Eject a Kobo after a sync that changed it (SPEC-MOUNT.md §4).
    bool ejectAfterSync() const;
    void setEjectAfterSync(bool eject);
    // Set the series of Kobold's books in a Kobo's library as soon as the Kobo is connected
    // (SPEC-KOBO-BOOKS.md §3.2); on by default.
    bool seriesOnConnect() const;
    void setSeriesOnConnect(bool on);
    // Show the steps to group new books by series at the end of a sync; on by default.
    bool showSeriesSteps() const;
    void setShowSeriesSteps(bool show);

    // Application icon picked by the user: "peek", "sync" or "face" (SPEC-APP.md §12.1).
    QString appIcon() const;
    void setAppIcon(const QString &icon);

    // Known Kobos, sorted by name (SPEC-DEVICES.md §2).
    QList<KnownKobo> knownKobos() const;
    // Registers the Kobo or updates its product id and last-seen date; gives a new Kobo a
    // unique default name. Returns its current entry.
    KnownKobo registerKobo(const DeviceIdentity &identity, const QDateTime &seen = QDateTime::currentDateTimeUtc());
    // Returns false and sets errorString when the name is empty, contains '/', or is used
    // by another Kobo (ignoring case).
    bool renameKobo(const QString &serial, const QString &name, QString *errorString);
    // Finds a known Kobo by name (ignoring case) or serial.
    std::optional<KnownKobo> findKobo(const QString &nameOrSerial) const;

    // Sidebar sections the user collapsed in the application (SPEC-APP.md §5).
    QStringList collapsedSections() const;
    void setCollapsedSections(const QStringList &sections);

    // Serial of the Kobo the application showed last (SPEC-APP.md §4), or empty.
    QString lastKobo() const;
    void setLastKobo(const QString &serial);

    // Writes pending changes to disk.
    bool sync();

private:
    KSharedConfigPtr m_config;
};

} // namespace Kobold
