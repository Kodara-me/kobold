// SPDX-License-Identifier: MIT

#include "translations.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

void useBuildTreeTranslations()
{
    const QString buildBin = QFileInfo(QStringLiteral(KOBOLD_BUILD_DIR "/bin")).canonicalFilePath();
    if (buildBin.isEmpty() || QFileInfo(QCoreApplication::applicationDirPath()).canonicalFilePath() != buildBin) {
        return;
    }
    KLocalizedString::addDomainLocaleDir("kobold", QStringLiteral(KOBOLD_BUILD_DIR "/locale"));
}
