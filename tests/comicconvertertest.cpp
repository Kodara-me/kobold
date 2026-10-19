// SPDX-License-Identifier: MIT

#include "comicconverter.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace std::chrono_literals;

namespace
{
// Stub standing in for kcc-c2e; KOBOLD_STUB_MODE selects the behaviour and the arguments are
// written to $KOBOLD_STUB_ARGS. Arguments: --profile P --format EPUB --title T --output DIR
// [--manga-style] ARCHIVE.
const QByteArray StubScript = R"(#!/bin/sh
printf '%s\n' "$@" > "$KOBOLD_STUB_ARGS"
prev=""
for arg in "$@"; do
    [ "$prev" = "--output" ] && out="$arg"
    prev="$arg"
    archive="$arg"
done
case "$KOBOLD_STUB_MODE" in
    help) echo "comic2ebook v10.3.0 - Written by someone."; echo "  -p PROFILE  Device profile (Available options: K1, KV, KoMT, KoC, KoE) Kobo devices"; exit 0 ;;
    oldhelp) echo "comic2ebook v9.1.0"; exit 0 ;;
    success) cp "$archive" "$out/converted.kepub.epub"; exit 0 ;;
    two) touch "$out/a.kepub.epub" "$out/b.kepub.epub"; exit 0 ;;
    none) exit 0 ;;
    fail) echo "ERROR: 7z is missing!"; exit 1 ;;
    hang) sleep 30 & wait ;;
esac
exit 99
)";
}

class ComicConverterTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_stub;
    QString m_archive;
    QString m_argsFile;

    ComicConverter::Result convertWith(const QByteArray &mode, const QString &output, std::chrono::milliseconds timeout = 10s, bool rightToLeft = false)
    {
        qputenv("KOBOLD_STUB_MODE", mode);
        ComicConverter converter;
        converter.setExecutable(m_stub);
        converter.setTimeout(timeout);
        std::optional<ComicConverter::Result> result;
        converter
            .convert(m_archive, output, QStringLiteral("KoC"), QStringLiteral("Naruto - Tome 01"), QStringLiteral("Masashi Kishimoto"), rightToLeft, [&result](const ComicConverter::Result &r) {
                result = r;
            });
        [&]() {
            QTRY_VERIFY_WITH_TIMEOUT(result, 15000);
        }();
        return result.value_or(ComicConverter::Result{false, QStringLiteral("no result")});
    }

    QStringList passedArguments() const
    {
        QFile file(m_argsFile);
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_stub = m_dir.filePath(QStringLiteral("kcc-stub"));
        QFile stub(m_stub);
        QVERIFY(stub.open(QIODevice::WriteOnly));
        stub.write(StubScript);
        stub.close();
        QVERIFY(stub.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        m_archive = m_dir.filePath(QStringLiteral("Naruto 01.cbz"));
        QFile archive(m_archive);
        QVERIFY(archive.open(QIODevice::WriteOnly));
        archive.write("PK comic");

        m_argsFile = m_dir.filePath(QStringLiteral("args.txt"));
        qputenv("KOBOLD_STUB_ARGS", m_argsFile.toUtf8());
    }

    void successCopiesTheSingleOutput()
    {
        const QString output = m_dir.filePath(QStringLiteral("out.kepub.epub"));
        const ComicConverter::Result result = convertWith("success", output);
        QVERIFY2(result.ok, qPrintable(result.errorString));
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("PK comic"));

        const QStringList arguments = passedArguments();
        QCOMPARE(arguments.mid(0, 6),
                 QStringList({QStringLiteral("--profile"),
                              QStringLiteral("KoC"),
                              QStringLiteral("--format"),
                              QStringLiteral("EPUB"),
                              QStringLiteral("--title"),
                              QStringLiteral("Naruto - Tome 01")}));
        QCOMPARE(arguments.at(6), QStringLiteral("--output"));
        QCOMPARE(arguments.last(), m_archive);
        QVERIFY(!arguments.contains(QStringLiteral("--manga-style")));
        QCOMPARE(arguments.at(arguments.indexOf(QStringLiteral("--author")) + 1), QStringLiteral("Masashi Kishimoto"));
        QVERIFY2(!QFileInfo::exists(arguments.at(7)), "the work directory is removed");
    }

    // CAC 13
    void rightToLeftUsesMangaStyle()
    {
        const QString output = m_dir.filePath(QStringLiteral("rtl.kepub.epub"));
        const ComicConverter::Result result = convertWith("success", output, 10s, true);
        QVERIFY2(result.ok, qPrintable(result.errorString));
        const QStringList arguments = passedArguments();
        QCOMPARE(arguments.at(arguments.size() - 2), QStringLiteral("--manga-style"));
        QCOMPARE(arguments.last(), m_archive);
    }

    // CAC 10
    void severalOutputsFail()
    {
        const QString output = m_dir.filePath(QStringLiteral("two.kepub.epub"));
        const ComicConverter::Result result = convertWith("two", output);
        QVERIFY(!result.ok);
        QVERIFY2(result.errorString.contains(QLatin1String("2 KEPUB files")), qPrintable(result.errorString));
        QVERIFY(!QFile::exists(output));
    }

    // CAC 10
    void noOutputFails()
    {
        const ComicConverter::Result result = convertWith("none", m_dir.filePath(QStringLiteral("none.kepub.epub")));
        QVERIFY(!result.ok);
        QVERIFY(result.errorString.contains(QLatin1String("0 KEPUB files")));
    }

    // CAC 9
    void failureReportsOutput()
    {
        const QString output = m_dir.filePath(QStringLiteral("fail.kepub.epub"));
        const ComicConverter::Result result = convertWith("fail", output);
        QVERIFY(!result.ok);
        QVERIFY2(result.errorString.contains(QLatin1String("exit code 1")), qPrintable(result.errorString));
        QVERIFY2(result.errorString.contains(QLatin1String("7z is missing")), qPrintable(result.errorString));
        QVERIFY(!QFile::exists(output));
    }

    // CAC 11
    void timeoutStopsTheProcessGroup()
    {
        QElapsedTimer timer;
        timer.start();
        const ComicConverter::Result result = convertWith("hang", m_dir.filePath(QStringLiteral("hang.kepub.epub")), 300ms);
        QVERIFY(!result.ok);
        QVERIFY2(result.errorString.contains(QLatin1String("timed out")), qPrintable(result.errorString));
        QVERIFY(timer.elapsed() < 5000);
    }

    void probeReadsVersionAndProfiles()
    {
        qputenv("KOBOLD_STUB_MODE", "help");
        ComicConverter converter;
        converter.setExecutable(m_stub);
        std::optional<ComicConverter::ProbeResult> result;
        converter.probe([&result](const ComicConverter::ProbeResult &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QVERIFY(result->found);
        QVERIFY(result->supported);
        QCOMPARE(result->version, QStringLiteral("10.3.0"));
        QCOMPARE(result->profiles, QStringList({QStringLiteral("KoMT"), QStringLiteral("KoC"), QStringLiteral("KoE")}));

        qputenv("KOBOLD_STUB_MODE", "oldhelp");
        result.reset();
        converter.probe([&result](const ComicConverter::ProbeResult &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QVERIFY(!result->supported);

        converter.setExecutable(QString());
        result.reset();
        converter.probe([&result](const ComicConverter::ProbeResult &r) {
            result = r;
        });
        QVERIFY(result && !result->found);
    }

    void realKccConvertsAGeneratedArchive()
    {
        const QString kcc = ComicConverter::findExecutable();
        if (kcc.isEmpty() || QStandardPaths::findExecutable(QStringLiteral("7z")).isEmpty()) {
            QSKIP("kcc-c2e or 7z not installed");
        }

        // A one-page CBZ: KCC accepts any image, so a minimal valid PNG is enough.
        const QString archive = m_dir.filePath(QStringLiteral("real.cbz"));
        const QByteArray png = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAAAAAA6fptVAAAACklEQVR4nGP4DwABAQEAG7buVgAAAABJRU5ErkJggg==");
        {
            QTemporaryDir pages;
            QFile page(pages.filePath(QStringLiteral("001.png")));
            QVERIFY(page.open(QIODevice::WriteOnly));
            page.write(png);
            page.close();
            QProcess zip;
            zip.setWorkingDirectory(pages.path());
            zip.start(QStringLiteral("7z"), {QStringLiteral("a"), QStringLiteral("-tzip"), archive, QStringLiteral("001.png")});
            QVERIFY(zip.waitForFinished(30000));
            QCOMPARE(zip.exitCode(), 0);
        }

        ComicConverter converter;
        converter.setExecutable(kcc);
        converter.setTimeout(120s);
        const QString output = m_dir.filePath(QStringLiteral("real.kepub.epub"));
        std::optional<ComicConverter::Result> result;
        converter.convert(archive, output, QStringLiteral("KoC"), QStringLiteral("Real"), QString(), true, [&result](const ComicConverter::Result &r) {
            result = r;
        });
        QTRY_VERIFY_WITH_TIMEOUT(result, 120000);
        QVERIFY2(result->ok, qPrintable(result->errorString));
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.read(2) == "PK");
        file.close();

        // Manga mode marks the book right to left (CAC 13).
        QProcess list;
        list.start(QStringLiteral("7z"), {QStringLiteral("e"), QStringLiteral("-so"), output, QStringLiteral("*.opf"), QStringLiteral("-r")});
        QVERIFY(list.waitForFinished(30000));
        QVERIFY2(list.readAllStandardOutput().contains("page-progression-direction=\"rtl\""), "OPF is right to left");
    }
};

QTEST_GUILESS_MAIN(ComicConverterTest)

#include "comicconvertertest.moc"
