// SPDX-License-Identifier: MIT

// Development tool: opens the application window offscreen, goes through its main states and
// saves a screenshot of each in the directory given as first argument. It uses the same
// settings, keychain and Komga server as kobold-gui; set XDG_CONFIG_HOME, XDG_DATA_HOME,
// XDG_CACHE_HOME and KOBOLD_MEDIA_ROOT to keep real settings and devices out of it.
// The preview it runs is a dry run: it never changes a Kobo.

#include "apikeystore.h"
#include "appcontroller.h"
#include "komgaimageprovider.h"
#include "settings.h"

#include "aboutdata.h"
#include "translations.h"

#include <KAboutData>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QDir>
#include <QNetworkAccessManager>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickItem>
#include <QQuickWindow>
#include <QEventLoop>
#include <QTimer>

#include <cstdio>
#include <functional>


namespace
{
// Calls next once condition() holds (checked every 200 ms, at most timeoutMs), after settleMs.
void waitFor(std::function<bool()> condition, int timeoutMs, int settleMs, std::function<void()> next)
{
    auto *timer = new QTimer;
    auto elapsed = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, condition, timeoutMs, settleMs, next, elapsed]() {
        *elapsed += 200;
        if (condition() || *elapsed >= timeoutMs) {
            timer->stop();
            timer->deleteLater();
            QTimer::singleShot(settleMs, next);
        }
    });
    timer->start(200);
}

void save(QQuickWindow *window, const QString &directory, const QString &name)
{
    const QString path = QDir(directory).filePath(name);
    window->grabWindow().save(path);
    std::printf("saved %s\n", qPrintable(path));
    std::fflush(stdout);
}
}

int main(int argc, char *argv[])
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: kobold-gui-screenshots <output directory> [series title]\n");
        return 2;
    }
    const QString directory = QString::fromLocal8Bit(argv[1]);
    const QString seriesTitle = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();
    QDir().mkpath(directory);

    KLocalizedString::setApplicationDomain("kobold");
    useBuildTreeTranslations();
    KAboutData aboutData = koboldAboutData();
    KAboutData::setApplicationData(aboutData);
    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    Kobold::Settings settings;
    Kobold::ApiKeyStore apiKeyStore;
    QNetworkAccessManager network;
    Kobold::AppController controller(&settings, &apiKeyStore, &network);
    Kobold::AppController::setInstance(&controller);

    QQmlApplicationEngine engine;
    KLocalization::setupLocalizedContext(&engine);
    engine.addImageProvider(QStringLiteral("komga"), new Kobold::KomgaImageProvider(controller.thumbnails()));
    engine.loadFromModule("me.kodara.kobold", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    window->resize(1440, 860);
    controller.start();

    // Settings → Kobos → Manage Books on the Kobo… (SPEC-KOBO-BOOKS.md §6); read only.
    const auto pause = [](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };
    const auto booksWindow = [&]() {
        for (QWindow *other : QGuiApplication::topLevelWindows()) {
            if (other != window && other->isVisible()) {
                other->close();
            }
        }
        QObject *configuration = window->findChild<QObject *>(QStringLiteral("configuration"));
        QMetaObject::invokeMethod(configuration, "open", Q_ARG(QVariant, QStringLiteral("kobos")));
        pause(2500);
        QQuickWindow *settings = nullptr;
        for (QWindow *other : QGuiApplication::topLevelWindows()) {
            auto *quickWindow = qobject_cast<QQuickWindow *>(other);
            if (quickWindow && quickWindow != window && quickWindow->isVisible()) {
                settings = quickWindow;
            }
        }
        // Repeater delegates are not QObject children: look through the item tree.
        std::function<QObject *(QQuickItem *)> findItem = [&findItem](QQuickItem *item) -> QObject * {
            if (item->objectName() == QLatin1String("manageBooks")) {
                return item;
            }
            for (QQuickItem *child : item->childItems()) {
                if (QObject *found = findItem(child)) {
                    return found;
                }
            }
            return nullptr;
        };
        QObject *button = settings ? findItem(settings->contentItem()) : nullptr;
        if (button) {
            QMetaObject::invokeMethod(button, "clicked");
            pause(5000);
            for (QWindow *other : QGuiApplication::topLevelWindows()) {
                auto *quickWindow = qobject_cast<QQuickWindow *>(other);
                if (quickWindow && quickWindow != window && quickWindow != settings && quickWindow->isVisible()) {
                    save(quickWindow, directory, QStringLiteral("10-kobo-books.png"));
                }
            }
        }
        app.quit();
    };

    const auto settingsWindow = [&]() {
        QObject *configuration = window->findChild<QObject *>(QStringLiteral("configuration"));
        QMetaObject::invokeMethod(configuration, "open", Q_ARG(QVariant, QStringLiteral("kobos")));
        // Tall enough to show a whole Kobo card, Komga sync included.
        QTimer::singleShot(1500, [&]() {
            for (QWindow *other : QGuiApplication::topLevelWindows()) {
                if (other != window && other->isVisible()) {
                    other->resize(900, 1100);
                }
            }
        });
        QTimer::singleShot(3000, [&, configuration]() {
            for (QWindow *other : QGuiApplication::topLevelWindows()) {
                auto *quickWindow = qobject_cast<QQuickWindow *>(other);
                if (quickWindow && quickWindow != window && quickWindow->isVisible()) {
                    save(quickWindow, directory, QStringLiteral("5-settings.png"));
                    quickWindow->close();
                }
            }
            QTimer::singleShot(1000, [configuration]() {
                QMetaObject::invokeMethod(configuration, "open", Q_ARG(QVariant, QStringLiteral("about")));
            });
            // Tall enough to show the authors and credits below the libraries.
            QTimer::singleShot(2500, [&]() {
                for (QWindow *other : QGuiApplication::topLevelWindows()) {
                    if (other != window && other->isVisible()) {
                        other->resize(900, 1500);
                    }
                }
            });
            QTimer::singleShot(6000, [&, configuration]() {
                for (QWindow *other : QGuiApplication::topLevelWindows()) {
                    auto *quickWindow = qobject_cast<QQuickWindow *>(other);
                    if (quickWindow && quickWindow != window && quickWindow->isVisible()) {
                        save(quickWindow, directory, QStringLiteral("6-about.png"));
                        quickWindow->close();
                    }
                }
                QTimer::singleShot(1000, [configuration]() {
                    QMetaObject::invokeMethod(configuration, "open", Q_ARG(QVariant, QStringLiteral("appearance")));
                });
                QTimer::singleShot(4000, [&, configuration]() {
                    for (QWindow *other : QGuiApplication::topLevelWindows()) {
                        auto *quickWindow = qobject_cast<QQuickWindow *>(other);
                        if (quickWindow && quickWindow != window && quickWindow->isVisible()) {
                            save(quickWindow, directory, QStringLiteral("7-appearance.png"));
                            quickWindow->close();
                        }
                    }
                    QTimer::singleShot(1000, [configuration]() {
                        QMetaObject::invokeMethod(configuration, "open", Q_ARG(QVariant, QStringLiteral("parental")));
                    });
                    QTimer::singleShot(4000, [&]() {
                        for (QWindow *other : QGuiApplication::topLevelWindows()) {
                            auto *quickWindow = qobject_cast<QQuickWindow *>(other);
                            if (quickWindow && quickWindow != window && quickWindow->isVisible()) {
                                save(quickWindow, directory, QStringLiteral("8-parental.png"));
                                if (qEnvironmentVariableIsSet("KOBOLD_SCREENSHOT_PIN_DIALOG")) {
                                    QObject *dialog = quickWindow->findChild<QObject *>(QStringLiteral("parentalPinDialog"));
                                    std::fprintf(stderr, "pin dialog found: %d\n", dialog != nullptr);
                                    if (dialog) {
                                        QMetaObject::invokeMethod(dialog, "ask", Q_ARG(QVariant, QStringLiteral("enable")));
                                        QEventLoop pause;
                                        QTimer::singleShot(1500, &pause, &QEventLoop::quit);
                                        pause.exec();
                                        std::fprintf(stderr,
                                                     "opened: %d visible: %d parent: %s\n",
                                                     dialog->property("opened").toBool(),
                                                     dialog->property("visible").toBool(),
                                                     dialog->property("parent").value<QObject *>() ? dialog->property("parent").value<QObject *>()->metaObject()->className() : "none");
                                        save(quickWindow, directory, QStringLiteral("9-pin.png"));
                                    }
                                }
                            }
                        }
                        booksWindow();
                    });
                });
            });
        });
    };

    const auto narrow = [&]() {
        window->resize(760, 860);
        QTimer::singleShot(2000, [&]() {
            save(window, directory, QStringLiteral("4-narrow.png"));
            window->resize(1440, 860);
            QTimer::singleShot(1000, settingsWindow);
        });
    };

    const auto preview = [&]() {
        controller.sync(true);
        QTimer::singleShot(1200, [&]() {
            save(window, directory, QStringLiteral("3-preview-running.png"));
        });
        waitFor(
            [&]() {
                return !controller.busy();
            },
            120000,
            1500,
            [&]() {
                save(window, directory, QStringLiteral("3-preview.png"));
                if (QObject *dialog = window->findChild<QObject *>(QStringLiteral("syncProgressDialog"))) {
                    QMetaObject::invokeMethod(dialog, "close");
                }
                narrow();
            });
    };

    const auto library = [&]() {
        save(window, directory, QStringLiteral("1-start.png"));
        QString seriesId;
        for (const QVariantMap &row : controller.series()->items()) {
            if (seriesId.isEmpty() || row.value(QStringLiteral("title")).toString() == seriesTitle) {
                seriesId = row.value(QStringLiteral("id")).toString();
            }
        }
        controller.openItem(QStringLiteral("series"), seriesId);
        waitFor(
            [&]() {
                return !controller.booksLoading() && controller.books()->rowCount() > 0;
            },
            20000,
            4000,
            [&]() {
                save(window, directory, QStringLiteral("2-series.png"));
                // The shelf editors (SPEC-SHELF.md §4.4), when the opened series is on a shelf.
                const QString current = controller.currentItem().value(QStringLiteral("id")).toString();
                QObject *editor = window->findChild<QObject *>(QStringLiteral("shelfEditorDialog"));
                if (qEnvironmentVariableIsSet("KOBOLD_SCREENSHOT_SHELF_EDITOR") && editor && current.startsWith(QLatin1String("shelf:"))) {
                    QMetaObject::invokeMethod(editor, "openSeries", Q_ARG(QVariant, current));
                    pause(2500);
                    save(window, directory, QStringLiteral("2b-series-editor.png"));
                    const QStringList books = controller.seriesBookIds(current);
                    QMetaObject::invokeMethod(editor, "openBooks", Q_ARG(QVariant, QStringList{books.value(0)}));
                    pause(2500);
                    save(window, directory, QStringLiteral("2c-book-editor.png"));
                    QMetaObject::invokeMethod(editor, "openBooks", Q_ARG(QVariant, books));
                    pause(2500);
                    save(window, directory, QStringLiteral("2d-books-editor.png"));
                    QMetaObject::invokeMethod(editor, "close");
                    pause(500);
                }
                preview();
            });
    };

    waitFor(
        [&]() {
            return controller.komgaState() != QLatin1String("connecting") && !controller.libraryLoading() && controller.series()->rowCount() > 0;
        },
        30000,
        5000,
        library);
    return app.exec();
}
