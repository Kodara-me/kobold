// SPDX-License-Identifier: MIT

#include "apikeystore.h"
#include "appcontroller.h"
#include "komgaimageprovider.h"
#include "settings.h"

#include "aboutdata.h"
#include "translations.h"

#include <KAboutData>
#include <KCrash>
#include <KIconTheme>
#include <KLocalizedQmlContext>
#include <KLocalizedString>

#include <QApplication>
#include <QNetworkAccessManager>
#include <QQmlApplicationEngine>
#include <QQuickStyle>


int main(int argc, char *argv[])
{
    KIconTheme::initTheme();
    QApplication app(argc, argv);
    KLocalizedString::setApplicationDomain("kobold");
    useBuildTreeTranslations();

    KAboutData aboutData = koboldAboutData();
    KAboutData::setApplicationData(aboutData);
    KCrash::initialize();

    if (qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));
    }

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

    controller.start();
    return app.exec();
}
