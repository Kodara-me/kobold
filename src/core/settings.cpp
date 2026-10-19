// SPDX-License-Identifier: MIT

#include "settings.h"
#include "deviceidentity.h"

#include <KConfigGroup>
#include <KLocalizedString>

namespace Kobold
{

namespace
{
const QString KomgaGroup = QStringLiteral("Komga");
const QString DeviceGroup = QStringLiteral("Device");
const QString ConversionGroup = QStringLiteral("Conversion");

QString deviceGroup(const QString &serial)
{
    return QStringLiteral("Device ") + serial;
}
}

Settings::Settings()
    : Settings(KSharedConfig::openConfig(QStringLiteral("koboldrc"), KConfig::SimpleConfig))
{
}

Settings::Settings(KSharedConfigPtr config)
    : m_config(std::move(config))
{
}

QUrl Settings::komgaUrl() const
{
    return QUrl(m_config->group(KomgaGroup).readEntry("Url", DefaultKomgaUrl));
}

bool Settings::setKomgaUrl(const QString &url, QString *errorString)
{
    QUrl parsed(url.trimmed(), QUrl::StrictMode);
    const QString scheme = parsed.scheme().toLower();
    if (!parsed.isValid() || (scheme != QLatin1String("https") && scheme != QLatin1String("http")) || parsed.host().isEmpty()) {
        *errorString = i18n("invalid Komga URL \"%1\": expected http(s)://host[:port][/path]", url);
        return false;
    }
    if (!parsed.userInfo().isEmpty() || parsed.hasQuery() || parsed.hasFragment()) {
        *errorString = i18n("invalid Komga URL \"%1\": credentials, query and fragment are not allowed", url);
        return false;
    }

    QString path = parsed.path();
    while (path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    parsed.setPath(path);
    parsed.setScheme(scheme);
    parsed.setHost(parsed.host().toLower());

    m_config->group(KomgaGroup).writeEntry("Url", parsed.toString());
    return true;
}

QString Settings::booksFolder() const
{
    return m_config->group(DeviceGroup).readEntry("BooksFolder", DefaultBooksFolder);
}

std::chrono::seconds Settings::conversionTimeout() const
{
    const int seconds = m_config->group(ConversionGroup).readEntry("TimeoutSeconds", int(DefaultConversionTimeout.count()));
    return seconds > 0 ? std::chrono::seconds(seconds) : DefaultConversionTimeout;
}

std::chrono::seconds Settings::comicConversionTimeout() const
{
    const int seconds = m_config->group(ConversionGroup).readEntry("ComicTimeoutSeconds", int(DefaultComicConversionTimeout.count()));
    return seconds > 0 ? std::chrono::seconds(seconds) : DefaultComicConversionTimeout;
}

QString Settings::comicProfileOverride(const QString &serial) const
{
    return m_config->group(deviceGroup(serial)).readEntry("ComicProfile", QString());
}

void Settings::setComicProfileOverride(const QString &serial, const QString &profile)
{
    m_config->group(deviceGroup(serial)).writeEntry("ComicProfile", profile);
}

void Settings::clearComicProfileOverride(const QString &serial)
{
    m_config->group(deviceGroup(serial)).deleteEntry("ComicProfile");
}

QString Settings::koboSyncKeyId(const QString &serial) const
{
    return m_config->group(deviceGroup(serial)).readEntry("KoboSyncKeyId", QString());
}

void Settings::setKoboSyncKeyId(const QString &serial, const QString &keyId)
{
    m_config->group(deviceGroup(serial)).writeEntry("KoboSyncKeyId", keyId);
}

bool Settings::koboSyncInstalled(const QString &serial) const
{
    return m_config->group(deviceGroup(serial)).readEntry("KoboSyncInstalled", false);
}

void Settings::setKoboSyncInstalled(const QString &serial, bool installed)
{
    m_config->group(deviceGroup(serial)).writeEntry("KoboSyncInstalled", installed);
}

QString Settings::koboSyncOriginalEndpoint(const QString &serial) const
{
    return m_config->group(deviceGroup(serial)).readEntry("KoboSyncOriginalEndpoint", QString());
}

void Settings::setKoboSyncOriginalEndpoint(const QString &serial, const QString &endpoint)
{
    m_config->group(deviceGroup(serial)).writeEntry("KoboSyncOriginalEndpoint", endpoint);
}

ParentalRules Settings::parentalRules() const
{
    const KConfigGroup group = m_config->group(QStringLiteral("Parental"));
    ParentalRules rules;
    rules.enabled = group.readEntry("Enabled", false);
    rules.ageLimit = group.readEntry("AgeLimit", 18);
    rules.keywords = group.hasKey("Keywords") ? group.readEntry("Keywords", QStringList()) : ParentalRules::defaultKeywords();
    return rules;
}

void Settings::setParentalRules(const ParentalRules &rules)
{
    KConfigGroup group = m_config->group(QStringLiteral("Parental"));
    group.writeEntry("Enabled", rules.enabled);
    group.writeEntry("AgeLimit", rules.ageLimit);
    group.writeEntry("Keywords", rules.keywords);
}

int Settings::parentalAutoLockMinutes() const
{
    return m_config->group(QStringLiteral("Parental")).readEntry("AutoLockMinutes", 15);
}

void Settings::setParentalAutoLockMinutes(int minutes)
{
    m_config->group(QStringLiteral("Parental")).writeEntry("AutoLockMinutes", minutes);
}

QString Settings::koboVolumeUuid(const QString &serial) const
{
    return m_config->group(deviceGroup(serial)).readEntry("VolumeUuid", QString());
}

void Settings::setKoboVolumeUuid(const QString &serial, const QString &uuid)
{
    m_config->group(deviceGroup(serial)).writeEntry("VolumeUuid", uuid);
}

std::optional<KnownKobo> Settings::findKoboByVolumeUuid(const QString &uuid) const
{
    if (uuid.isEmpty()) {
        return std::nullopt;
    }
    for (const KnownKobo &kobo : knownKobos()) {
        if (koboVolumeUuid(kobo.serial) == uuid) {
            return kobo;
        }
    }
    return std::nullopt;
}

bool Settings::backupBeforeSync(const QString &serial) const
{
    return m_config->group(deviceGroup(serial)).readEntry("BackupBeforeSync", false);
}

void Settings::setBackupBeforeSync(const QString &serial, bool backup)
{
    m_config->group(deviceGroup(serial)).writeEntry("BackupBeforeSync", backup);
}

bool Settings::seriesOnConnect() const
{
    return m_config->group(QStringLiteral("Application")).readEntry("SeriesOnConnect", true);
}

void Settings::setSeriesOnConnect(bool on)
{
    m_config->group(QStringLiteral("Application")).writeEntry("SeriesOnConnect", on);
}

bool Settings::showSeriesSteps() const
{
    return m_config->group(QStringLiteral("Application")).readEntry("ShowSeriesSteps", true);
}

void Settings::setShowSeriesSteps(bool show)
{
    m_config->group(QStringLiteral("Application")).writeEntry("ShowSeriesSteps", show);
}

bool Settings::ejectAfterSync() const
{
    return m_config->group(QStringLiteral("Application")).readEntry("EjectAfterSync", false);
}

void Settings::setEjectAfterSync(bool eject)
{
    m_config->group(QStringLiteral("Application")).writeEntry("EjectAfterSync", eject);
}

QString Settings::appIcon() const
{
    return m_config->group(QStringLiteral("Application")).readEntry("Icon", QStringLiteral("peek"));
}

void Settings::setAppIcon(const QString &icon)
{
    m_config->group(QStringLiteral("Application")).writeEntry("Icon", icon);
}

QList<KnownKobo> Settings::knownKobos() const
{
    QList<KnownKobo> kobos;
    const QStringList groups = m_config->groupList();
    for (const QString &group : groups) {
        if (!group.startsWith(QLatin1String("Device "))) {
            continue;
        }
        const KConfigGroup config = m_config->group(group);
        if (!config.hasKey("Name")) {
            continue;
        }
        KnownKobo kobo;
        kobo.serial = group.mid(7);
        kobo.name = config.readEntry("Name", QString());
        kobo.productId = config.readEntry("ProductId", 0);
        kobo.lastSeen = QDateTime::fromString(config.readEntry("LastSeen", QString()), Qt::ISODate);
        kobos.append(kobo);
    }
    std::sort(kobos.begin(), kobos.end(), [](const KnownKobo &a, const KnownKobo &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return kobos;
}

KnownKobo Settings::registerKobo(const DeviceIdentity &identity, const QDateTime &seen)
{
    KConfigGroup config = m_config->group(deviceGroup(identity.serial));
    if (!config.hasKey("Name")) {
        const auto model = koboModel(identity.productId);
        const QString base = model ? model->name : QStringLiteral("Kobo ") + identity.serial;
        QString name = base;
        for (int suffix = 2; findKobo(name); ++suffix) {
            name = QStringLiteral("%1 (%2)").arg(base).arg(suffix);
        }
        config.writeEntry("Name", name);
    }
    if (identity.productId != 0) {
        config.writeEntry("ProductId", identity.productId);
    }
    config.writeEntry("LastSeen", seen.toUTC().toString(Qt::ISODate));
    return *findKobo(identity.serial);
}

bool Settings::renameKobo(const QString &serial, const QString &name, QString *errorString)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.contains(QLatin1Char('/'))) {
        *errorString = i18n("invalid Kobo name \"%1\"", name);
        return false;
    }
    const auto existing = findKobo(trimmed);
    if (existing && existing->serial != serial) {
        *errorString = i18n("the name \"%1\" is already used by the Kobo %2", trimmed, existing->serial);
        return false;
    }
    KConfigGroup config = m_config->group(deviceGroup(serial));
    if (!config.hasKey("Name")) {
        *errorString = i18n("unknown Kobo %1", serial);
        return false;
    }
    config.writeEntry("Name", trimmed);
    return true;
}

std::optional<KnownKobo> Settings::findKobo(const QString &nameOrSerial) const
{
    const QString wanted = nameOrSerial.trimmed();
    const QList<KnownKobo> kobos = knownKobos();
    for (const KnownKobo &kobo : kobos) {
        if (kobo.serial == wanted || kobo.name.compare(wanted, Qt::CaseInsensitive) == 0) {
            return kobo;
        }
    }
    return std::nullopt;
}

QStringList Settings::collapsedSections() const
{
    return m_config->group(QStringLiteral("Application")).readEntry("CollapsedSections", QStringList());
}

void Settings::setCollapsedSections(const QStringList &sections)
{
    m_config->group(QStringLiteral("Application")).writeEntry("CollapsedSections", sections);
}

QString Settings::lastKobo() const
{
    return m_config->group(QStringLiteral("Application")).readEntry("CurrentKobo", QString());
}

void Settings::setLastKobo(const QString &serial)
{
    m_config->group(QStringLiteral("Application")).writeEntry("CurrentKobo", serial);
}

bool Settings::sync()
{
    return m_config->sync();
}

} // namespace Kobold
