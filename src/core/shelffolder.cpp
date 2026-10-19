// SPDX-License-Identifier: MIT

#include "shelffolder.h"

#include <KLocalizedString>

#include <QDir>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace Kobold::ShelfFolder
{

QString defaultParent()
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

std::optional<Shelf> createLibrary(ShelfDatabase *database, const QString &parent, QString *errorString)
{
    const QString baseName = i18nc("@info folder and shelf name", "Kobold Library");
    QString name = baseName;
    QDir folder(QDir(parent).filePath(name));
    // An empty folder of that name is taken as it is; another one gets a number.
    for (int number = 2; folder.exists() && !folder.isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot); ++number) {
        name = QStringLiteral("%1 %2").arg(baseName).arg(number);
        folder.setPath(QDir(parent).filePath(name));
    }
    if (!QDir().mkpath(folder.path())) {
        *errorString = i18n("cannot create %1", folder.path());
        return std::nullopt;
    }

    // Dolphin and the file dialogs read the folder's icon here.
    QSaveFile directory(folder.filePath(QStringLiteral(".directory")));
    if (directory.open(QIODevice::WriteOnly)) {
        directory.write("[Desktop Entry]\nIcon=" + QByteArray(IconName.data(), IconName.size()) + "\n");
        directory.commit();
    }

    const QList<Shelf> shelves = database->shelves();
    const auto taken = [&shelves](const QString &candidate) {
        return std::any_of(shelves.cbegin(), shelves.cend(), [&candidate](const Shelf &shelf) {
            return shelf.name.compare(candidate, Qt::CaseInsensitive) == 0;
        });
    };
    QString shelfName = name;
    for (int number = 2; taken(shelfName); ++number) {
        shelfName = QStringLiteral("%1 %2").arg(baseName).arg(number);
    }
    return database->addShelf(shelfName, folder.path(), errorString);
}

} // namespace Kobold::ShelfFolder
