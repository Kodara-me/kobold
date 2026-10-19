// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <functional>

namespace Kobold
{

// Runs Kindle Comic Converter (kcc-c2e) to turn CBZ/CBR archives into fixed-layout KEPUB
// (SPEC-COMICS.md §5). Left-to-right reading only.
class ComicConverter : public QObject
{
    Q_OBJECT

public:
    static inline const QString MinimumVersion = QStringLiteral("10.0");
    static constexpr qsizetype MaxErrorOutputBytes = 4096;

    struct Result {
        bool ok = false;
        QString errorString;
    };

    struct ProbeResult {
        // False when kcc-c2e cannot be started.
        bool found = false;
        QString version;
        // True when version >= MinimumVersion.
        bool supported = false;
        // Profiles listed by `kcc-c2e --help`.
        QStringList profiles;
        QString errorString;
    };

    explicit ComicConverter(QObject *parent = nullptr);

    // Path of kcc-c2e in PATH, or empty.
    static QString findExecutable();

    // Defaults to findExecutable().
    QString executable() const;
    void setExecutable(const QString &executable);
    void setTimeout(std::chrono::milliseconds timeout);

    // Converts archivePath with the KCC profile and writes the KEPUB to outputPath; rightToLeft
    // selects KCC's manga mode. An existing outputPath is replaced; on failure outputPath does
    // not exist.
    void convert(const QString &archivePath,
                 const QString &outputPath,
                 const QString &profile,
                 const QString &title,
                 const QString &author,
                 bool rightToLeft,
                 std::function<void(const Result &)> callback);

    // Runs "kcc-c2e --help" and reads the version banner and the profile list.
    void probe(std::function<void(const ProbeResult &)> callback);

private:
    QString m_executable;
    std::chrono::milliseconds m_timeout{std::chrono::seconds(1800)};
};

} // namespace Kobold
