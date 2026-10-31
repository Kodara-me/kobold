// SPDX-License-Identifier: MIT
//
// Stand-ins for KConfig, KSharedConfig and KConfigGroup over QSettings (INI), used when
// KOBOLD_USE_KDE is OFF. They cover what Kobold calls: groups, readEntry, writeEntry,
// deleteEntry, hasKey, groupList, sync and reparseConfiguration. The file is the same
// koboldrc, written as INI.

#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QVariant>

#include <memory>

class KConfig
{
public:
    enum OpenFlag {
        SimpleConfig = 0,
        FullConfig = 1,
    };

    explicit KConfig(const QString &fileName, OpenFlag = FullConfig)
        : m_settings(std::make_shared<QSettings>(resolve(fileName), QSettings::IniFormat))
    {
    }

    QStringList groupList() const
    {
        return m_settings->childGroups();
    }

    void deleteGroup(const QString &group)
    {
        m_settings->remove(group);
        m_settings->sync();
    }

    bool sync()
    {
        m_settings->sync();
        return m_settings->status() == QSettings::NoError;
    }

    void reparseConfiguration()
    {
        m_settings->sync();
    }

    std::shared_ptr<QSettings> settings() const
    {
        return m_settings;
    }

protected:
    // A relative name lives in the generic configuration folder, as KConfig puts it.
    static QString resolve(const QString &fileName)
    {
        if (QFileInfo(fileName).isAbsolute()) {
            return fileName;
        }
        return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(fileName);
    }

    std::shared_ptr<QSettings> m_settings;
};

class KConfigGroup
{
public:
    KConfigGroup() = default;
    KConfigGroup(std::shared_ptr<QSettings> settings, const QString &group)
        : m_settings(std::move(settings))
        , m_group(group)
    {
    }

    bool hasKey(const char *key) const
    {
        return m_settings && m_settings->contains(path(key));
    }

    QString readEntry(const char *key, const QString &defaultValue) const
    {
        return value(key, defaultValue).toString();
    }

    QString readEntry(const char *key, const char *defaultValue) const
    {
        return value(key, QString::fromUtf8(defaultValue)).toString();
    }

    bool readEntry(const char *key, bool defaultValue) const
    {
        return value(key, defaultValue).toBool();
    }

    int readEntry(const char *key, int defaultValue) const
    {
        return value(key, defaultValue).toInt();
    }

    qint64 readEntry(const char *key, qint64 defaultValue) const
    {
        return value(key, defaultValue).toLongLong();
    }

    QStringList readEntry(const char *key, const QStringList &defaultValue) const
    {
        return value(key, defaultValue).toStringList();
    }

    template<typename T>
    void writeEntry(const char *key, const T &entry)
    {
        if (!m_settings) {
            return;
        }
        m_settings->setValue(path(key), QVariant::fromValue(entry));
        m_settings->sync();
    }

    void deleteEntry(const char *key)
    {
        if (!m_settings) {
            return;
        }
        m_settings->remove(path(key));
        m_settings->sync();
    }

    QStringList keyList() const
    {
        if (!m_settings) {
            return {};
        }
        m_settings->beginGroup(m_group);
        const QStringList keys = m_settings->childKeys();
        m_settings->endGroup();
        return keys;
    }

private:
    QString path(const char *key) const
    {
        return m_group + QLatin1Char('/') + QString::fromUtf8(key);
    }

    QVariant value(const char *key, const QVariant &defaultValue) const
    {
        return m_settings ? m_settings->value(path(key), defaultValue) : defaultValue;
    }

    std::shared_ptr<QSettings> m_settings;
    QString m_group;
};

class KSharedConfig : public KConfig
{
public:
    using Ptr = std::shared_ptr<KSharedConfig>;

    static Ptr openConfig(const QString &fileName, OpenFlag flags = FullConfig)
    {
        return Ptr(new KSharedConfig(fileName, flags));
    }

    KConfigGroup group(const QString &name) const
    {
        return KConfigGroup(m_settings, name);
    }

private:
    KSharedConfig(const QString &fileName, OpenFlag flags)
        : KConfig(fileName, flags)
    {
    }
};

using KSharedConfigPtr = KSharedConfig::Ptr;
