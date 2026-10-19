// SPDX-License-Identifier: MIT

import QtQuick
import org.kde.kirigamiaddons.formcard as FormCard
import me.kodara.kobold

// Settings: cover cache (SPEC-APP.md §9, §10).
FormCard.FormCardPage {
    id: page

    title: i18nc("@title", "Cover Cache")

    FormCard.FormHeader {
        title: i18nc("@title:group", "Covers")
    }

    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: i18n("Size on disk")
            description: Qt.locale().formattedDataSize(AppController.coverCacheSize, 1)
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Clear Cover Cache")
            description: i18n("Covers are downloaded again from Komga when needed.")
            icon.name: "edit-clear"
            onClicked: AppController.clearCoverCache()
        }
    }
}
