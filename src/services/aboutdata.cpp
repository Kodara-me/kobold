// SPDX-License-Identifier: MIT

#include "aboutdata.h"

#include <KLocalizedString>

#include <QUrl>

#include "kobold_version.h"

KAboutData koboldAboutData()
{
    KAboutData about(QStringLiteral("kobold"),
                     i18n("Kobold"),
                     QStringLiteral(KOBOLD_VERSION_STRING),
                     i18n("Synchronize Komga books with a Kobo e-reader"),
                     KAboutLicense::MIT);

    // The author's email address stays out of a public page (SPEC-ABOUT.md §2).
    about.addAuthor(QStringLiteral("Jonathan St-Michel"),
                    i18nc("@info:credit", "Author and maintainer"),
                    QString(),
                    QStringLiteral("https://github.com/jstmichel"),
                    QUrl(QStringLiteral("https://avatars.githubusercontent.com/jstmichel")));
    about.addCredit(QStringLiteral("Kira"),
                    i18nc("@info:credit", "AI pair programmer: specifications and code"),
                    QStringLiteral("kira@kodara.me"),
                    QStringLiteral("https://github.com/kira-kodara"),
                    QUrl(QStringLiteral("https://avatars.githubusercontent.com/kira-kodara")));
    about.setTranslator(i18nc("NAME OF TRANSLATORS", "Your names"), i18nc("EMAIL OF TRANSLATORS", "Your emails"));

    // The project on GitHub; bugs go to its issues, not to KDE's tracker (§4).
    about.setHomepage(QStringLiteral("https://github.com/jstmichel/kobold"));
    about.setBugAddress(QByteArrayLiteral("https://github.com/jstmichel/kobold/issues"));
    about.setDesktopFileName(QStringLiteral("me.kodara.kobold"));
    // Bundled with the application (src/app/CMakeLists.txt), so the About page shows it
    // even when the icon is not installed; the application replaces it with the icon the
    // user picked. The CLI ignores it.
    about.setProgramLogo(QUrl(QStringLiteral("qrc:/qt/qml/me/kodara/kobold/icons/peek.svg")));
    return about;
}
