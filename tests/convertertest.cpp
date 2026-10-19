// SPDX-License-Identifier: MIT

#include "converter.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Kobold;
using namespace std::chrono_literals;

namespace
{
// Stub standing in for kepubify; its behaviour is chosen by KOBOLD_STUB_MODE.
// Arguments as passed by Converter::convert: --output <output> <input>.
const QByteArray StubScript = R"(#!/bin/sh
case "$KOBOLD_STUB_MODE" in
    version) echo "kepubify v4.0.4"; exit 0 ;;
    oldversion) echo "kepubify 3.1.0"; exit 0 ;;
    success) cp "$3" "$2"; exit 0 ;;
    fail) echo "boom: invalid epub" >&2; exit 3 ;;
    partial) echo partial > "$2"; exit 1 ;;
    nooutput) exit 0 ;;
    hang) exec sleep 30 ;;
esac
exit 99
)";
}

class ConverterTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_stub;
    QString m_input;

    Converter::Result convertWith(const QByteArray &mode, const QString &output, std::chrono::milliseconds timeout = 10s)
    {
        qputenv("KOBOLD_STUB_MODE", mode);
        Converter converter;
        converter.setExecutable(m_stub);
        converter.setTimeout(timeout);
        std::optional<Converter::Result> result;
        converter.convert(m_input, output, [&result](const Converter::Result &r) {
            result = r;
        });
        [&]() {
            QTRY_VERIFY_WITH_TIMEOUT(result, 15000);
        }();
        return result.value_or(Converter::Result{false, QStringLiteral("no result")});
    }

    Converter::VersionResult probeWith(const QString &executable, const QByteArray &mode)
    {
        qputenv("KOBOLD_STUB_MODE", mode);
        Converter converter;
        converter.setExecutable(executable);
        std::optional<Converter::VersionResult> result;
        converter.probeVersion([&result](const Converter::VersionResult &r) {
            result = r;
        });
        [&]() {
            QTRY_VERIFY(result);
        }();
        return result.value_or(Converter::VersionResult{});
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_stub = m_dir.filePath(QStringLiteral("kepubify-stub"));
        QFile stub(m_stub);
        QVERIFY(stub.open(QIODevice::WriteOnly));
        stub.write(StubScript);
        stub.close();
        QVERIFY(stub.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        m_input = m_dir.filePath(QStringLiteral("input.epub"));
        QFile input(m_input);
        QVERIFY(input.open(QIODevice::WriteOnly));
        input.write("PK fake epub");
    }

    void successProducesOutput()
    {
        const QString output = m_dir.filePath(QStringLiteral("success.kepub.epub"));
        const Converter::Result result = convertWith("success", output);
        QVERIFY2(result.ok, qPrintable(result.errorString));
        QVERIFY(QFile::exists(output));
    }

    void existingOutputIsReplaced()
    {
        const QString output = m_dir.filePath(QStringLiteral("existing.kepub.epub"));
        {
            QFile file(output);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("old content that is longer than the new one");
        }
        const Converter::Result result = convertWith("success", output);
        QVERIFY2(result.ok, qPrintable(result.errorString));
        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("PK fake epub"));
    }

    void failureReportsExitCodeAndStderr()
    {
        const QString output = m_dir.filePath(QStringLiteral("fail.kepub.epub"));
        const Converter::Result result = convertWith("fail", output);
        QVERIFY(!result.ok);
        QVERIFY2(result.errorString.contains(QLatin1String("exit code 3")), qPrintable(result.errorString));
        QVERIFY2(result.errorString.contains(QLatin1String("boom: invalid epub")), qPrintable(result.errorString));
        QVERIFY(!QFile::exists(output));
    }

    void partialOutputIsRemovedOnFailure()
    {
        const QString output = m_dir.filePath(QStringLiteral("partial.kepub.epub"));
        const Converter::Result result = convertWith("partial", output);
        QVERIFY(!result.ok);
        QVERIFY(!QFile::exists(output));
    }

    void missingOutputIsAnError()
    {
        const QString output = m_dir.filePath(QStringLiteral("nooutput.kepub.epub"));
        const Converter::Result result = convertWith("nooutput", output);
        QVERIFY(!result.ok);
        QVERIFY2(result.errorString.contains(QLatin1String("did not produce")), qPrintable(result.errorString));
    }

    void timeoutKillsProcess()
    {
        const QString output = m_dir.filePath(QStringLiteral("hang.kepub.epub"));
        QElapsedTimer timer;
        timer.start();
        const Converter::Result result = convertWith("hang", output, 300ms);
        QVERIFY(!result.ok);
        QVERIFY2(result.errorString.contains(QLatin1String("timed out")), qPrintable(result.errorString));
        QVERIFY(timer.elapsed() < 5000);
    }

    void missingExecutable()
    {
        Converter converter;
        converter.setExecutable(m_dir.filePath(QStringLiteral("does-not-exist")));
        std::optional<Converter::Result> result;
        converter.convert(m_input, m_dir.filePath(QStringLiteral("x.kepub.epub")), [&result](const Converter::Result &r) {
            result = r;
        });
        QTRY_VERIFY(result);
        QVERIFY(!result->ok);
        QVERIFY2(result->errorString.contains(QLatin1String("cannot start")), qPrintable(result->errorString));

        converter.setExecutable(QString());
        result.reset();
        converter.convert(m_input, m_dir.filePath(QStringLiteral("x.kepub.epub")), [&result](const Converter::Result &r) {
            result = r;
        });
        QVERIFY(result);
        QVERIFY(result->errorString.contains(QLatin1String("not found")));
    }

    void versionProbe()
    {
        const Converter::VersionResult current = probeWith(m_stub, "version");
        QVERIFY(current.found);
        QCOMPARE(current.version, QStringLiteral("4.0.4"));
        QVERIFY(current.supported);

        const Converter::VersionResult old = probeWith(m_stub, "oldversion");
        QVERIFY(old.found);
        QVERIFY(!old.supported);
        QVERIFY(old.errorString.contains(QLatin1String("too old")));

        const Converter::VersionResult missing = probeWith(m_dir.filePath(QStringLiteral("does-not-exist")), "version");
        QVERIFY(!missing.found);
    }

    void realKepubifyConvertsFixture()
    {
        const QString kepubify = QStringLiteral(KOBOLD_KEPUBIFY_EXECUTABLE);
        if (kepubify.isEmpty()) {
            QSKIP("kepubify not installed");
        }

        Converter converter;
        converter.setExecutable(kepubify);
        const QString output = m_dir.filePath(QStringLiteral("real.kepub.epub"));
        std::optional<Converter::Result> result;
        converter.convert(QStringLiteral(KOBOLD_TEST_DATA_DIR "/minimal.epub"), output, [&result](const Converter::Result &r) {
            result = r;
        });
        QTRY_VERIFY_WITH_TIMEOUT(result, 30000);
        QVERIFY2(result->ok, qPrintable(result->errorString));

        QFile file(output);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.read(2) == "PK");
    }
};

QTEST_GUILESS_MAIN(ConverterTest)

#include "convertertest.moc"
