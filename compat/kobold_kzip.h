// SPDX-License-Identifier: MIT
//
// Stand-in for the part of KZip / KArchive that Kobold uses, on top of libarchive, for builds
// without KDE Frameworks. Reading lists the entries once and extracts a file on demand, so a
// large comic archive is never held in memory.

#pragma once

#include <archive.h>
#include <archive_entry.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QString>
#include <QStringList>

#include <map>
#include <memory>

class KArchiveEntry
{
public:
    virtual ~KArchiveEntry() = default;
    virtual bool isDirectory() const
    {
        return false;
    }
};

class KArchiveFile : public KArchiveEntry
{
public:
    KArchiveFile(QString archivePath, QString entryPath)
        : m_archivePath(std::move(archivePath))
        , m_entryPath(std::move(entryPath))
    {
    }

    // The uncompressed content, empty when the entry cannot be read.
    QByteArray data() const
    {
        archive *reader = archive_read_new();
        archive_read_support_format_zip(reader);
        QByteArray content;
#ifdef Q_OS_WIN
        const int opened = archive_read_open_filename_w(reader, reinterpret_cast<const wchar_t *>(m_archivePath.utf16()), 1024 * 1024);
#else
        const int opened = archive_read_open_filename(reader, QFile::encodeName(m_archivePath).constData(), 1024 * 1024);
#endif
        if (opened == ARCHIVE_OK) {
            archive_entry *entry = nullptr;
            while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
                const char *name = archive_entry_pathname_utf8(entry);
                if (QString::fromUtf8(name ? name : archive_entry_pathname(entry)) != m_entryPath) {
                    archive_read_data_skip(reader);
                    continue;
                }
                char buffer[64 * 1024];
                la_ssize_t count = 0;
                while ((count = archive_read_data(reader, buffer, sizeof(buffer))) > 0) {
                    content.append(buffer, qsizetype(count));
                }
                if (count < 0) {
                    content.clear();
                }
                break;
            }
        }
        archive_read_free(reader);
        return content;
    }

private:
    QString m_archivePath;
    QString m_entryPath;
};

class KArchiveDirectory : public KArchiveEntry
{
public:
    bool isDirectory() const override
    {
        return true;
    }

    QStringList entries() const
    {
        QStringList names;
        for (const auto &[name, child] : m_children) {
            names.append(name);
        }
        return names;
    }

    const KArchiveEntry *entry(const QString &name) const
    {
        const auto found = m_children.find(name);
        return found == m_children.end() ? nullptr : found->second.get();
    }

    // Accepts a path with slashes, like KArchiveDirectory::file.
    const KArchiveFile *file(const QString &path) const
    {
        const KArchiveDirectory *directory = this;
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
            directory = dynamic_cast<const KArchiveDirectory *>(directory->entry(parts.at(i)));
            if (!directory) {
                return nullptr;
            }
        }
        return parts.isEmpty() ? nullptr : dynamic_cast<const KArchiveFile *>(directory->entry(parts.last()));
    }

    KArchiveDirectory *addDirectory(const QString &name)
    {
        auto &slot = m_children[name];
        if (!slot) {
            slot = std::make_unique<KArchiveDirectory>();
        }
        return dynamic_cast<KArchiveDirectory *>(slot.get());
    }

    void addFile(const QString &name, std::unique_ptr<KArchiveFile> file)
    {
        m_children[name] = std::move(file);
    }

private:
    std::map<QString, std::unique_ptr<KArchiveEntry>> m_children;
};

class KZip
{
public:
    enum Compression {
        NoCompression,
        DeflateCompression,
    };

    explicit KZip(const QString &path)
        : m_path(path)
    {
    }

    ~KZip()
    {
        close();
    }

    KZip(const KZip &) = delete;
    KZip &operator=(const KZip &) = delete;

    bool open(QIODevice::OpenMode mode)
    {
        if (mode & QIODevice::WriteOnly) {
            return openForWriting();
        }
        return openForReading();
    }

    bool close()
    {
        bool ok = true;
        if (m_writer) {
            ok = !m_failed && archive_write_close(m_writer) == ARCHIVE_OK;
            archive_write_free(m_writer);
            m_writer = nullptr;
        }
        return ok;
    }

    const KArchiveDirectory *directory() const
    {
        return &m_root;
    }

    void setCompression(Compression compression)
    {
        m_compression = compression;
        if (m_writer) {
            applyCompression();
        }
    }

    bool writeFile(const QString &name, const QByteArray &data)
    {
        if (!m_writer || m_failed) {
            return false;
        }
        archive_entry *entry = archive_entry_new();
        const QByteArray utf8 = name.toUtf8();
        archive_entry_set_pathname_utf8(entry, utf8.constData());
        archive_entry_set_size(entry, data.size());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        bool ok = archive_write_header(m_writer, entry) == ARCHIVE_OK;
        if (ok && !data.isEmpty()) {
            ok = archive_write_data(m_writer, data.constData(), size_t(data.size())) == la_ssize_t(data.size());
        }
        archive_entry_free(entry);
        m_failed = m_failed || !ok;
        return ok;
    }

private:
    bool openForReading()
    {
        archive *reader = archive_read_new();
        archive_read_support_format_zip(reader);
#ifdef Q_OS_WIN
        const int opened = archive_read_open_filename_w(reader, reinterpret_cast<const wchar_t *>(m_path.utf16()), 1024 * 1024);
#else
        const int opened = archive_read_open_filename(reader, QFile::encodeName(m_path).constData(), 1024 * 1024);
#endif
        if (opened != ARCHIVE_OK) {
            archive_read_free(reader);
            return false;
        }
        archive_entry *entry = nullptr;
        int status = ARCHIVE_OK;
        while ((status = archive_read_next_header(reader, &entry)) == ARCHIVE_OK || status == ARCHIVE_WARN) {
            const char *name = archive_entry_pathname_utf8(entry);
            const QString path = QString::fromUtf8(name ? name : archive_entry_pathname(entry));
            if (archive_entry_filetype(entry) != AE_IFDIR && !path.endsWith(QLatin1Char('/'))) {
                addToTree(path);
            }
            archive_read_data_skip(reader);
        }
        archive_read_free(reader);
        return status == ARCHIVE_EOF;
    }

    void addToTree(const QString &path)
    {
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            return;
        }
        KArchiveDirectory *directory = &m_root;
        for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
            directory = directory->addDirectory(parts.at(i));
        }
        directory->addFile(parts.last(), std::make_unique<KArchiveFile>(m_path, path));
    }

    bool openForWriting()
    {
        m_writer = archive_write_new();
        archive_write_set_format_zip(m_writer);
        applyCompression();
#ifdef Q_OS_WIN
        const int opened = archive_write_open_filename_w(m_writer, reinterpret_cast<const wchar_t *>(m_path.utf16()));
#else
        const int opened = archive_write_open_filename(m_writer, QFile::encodeName(m_path).constData());
#endif
        if (opened != ARCHIVE_OK) {
            archive_write_free(m_writer);
            m_writer = nullptr;
            return false;
        }
        return true;
    }

    void applyCompression()
    {
        if (m_compression == NoCompression) {
            archive_write_zip_set_compression_store(m_writer);
        } else {
            archive_write_zip_set_compression_deflate(m_writer);
        }
    }

    QString m_path;
    KArchiveDirectory m_root;
    archive *m_writer = nullptr;
    Compression m_compression = DeflateCompression;
    bool m_failed = false;
};
