// SPDX-License-Identifier: MIT

#pragma once

#include <QObject>
#include <QString>

#include <chrono>
#include <functional>

namespace Kobold
{

// Runs kepubify to convert EPUB to KEPUB (SPEC.md §10). The process runs asynchronously
// and never blocks the event loop.
class Converter : public QObject
{
    Q_OBJECT

public:
    static inline const QString MinimumVersion = QStringLiteral("4.0");
    static constexpr qsizetype MaxErrorOutputBytes = 4096;

    struct Result {
        bool ok = false;
        QString errorString;
    };

    struct VersionResult {
        // False when kepubify cannot be started.
        bool found = false;
        QString version;
        // True when version >= MinimumVersion.
        bool supported = false;
        QString errorString;
    };

    explicit Converter(QObject *parent = nullptr);

    // Path of kepubify in PATH, or empty.
    static QString findExecutable();

    // Defaults to findExecutable().
    QString executable() const;
    void setExecutable(const QString &executable);

    void setTimeout(std::chrono::milliseconds timeout);

    // Converts inputPath to outputPath. An existing outputPath is replaced; on failure
    // outputPath does not exist.
    void convert(const QString &inputPath, const QString &outputPath, std::function<void(const Result &)> callback);

    // Runs "kepubify --version".
    void probeVersion(std::function<void(const VersionResult &)> callback);

private:
    QString m_executable;
    std::chrono::milliseconds m_timeout{std::chrono::seconds(300)};
};

} // namespace Kobold
