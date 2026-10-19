// SPDX-License-Identifier: MIT

import QtQuick
import org.kde.kirigamiaddons.formcard as FormCard
import me.kodara.kobold

// Settings: the checks of `kobold doctor` (SPEC-APP.md §10).
FormCard.FormCardPage {
    id: page

    title: i18nc("@title", "Diagnostics")

    Component.onCompleted: AppController.runDiagnostics()

    FormCard.FormHeader {
        title: i18nc("@title:group", "Checks")
    }

    FormCard.FormCard {
        Repeater {
            model: AppController.diagnostics

            delegate: FormCard.FormTextDelegate {
                required property int level
                required property string message
                required property string hint

                icon.name: level === 0 ? "data-success" : level === 1 ? "data-warning" : "data-error"
                text: message
                description: hint
            }
        }

        FormCard.FormButtonDelegate {
            text: AppController.diagnosticsRunning ? i18n("Checking…") : i18nc("@action:button", "Run Again")
            icon.name: "view-refresh"
            enabled: !AppController.diagnosticsRunning
            onClicked: AppController.runDiagnostics()
        }
    }
}
