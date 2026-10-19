// SPDX-License-Identifier: MIT

import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import me.kodara.kobold

// Settings: Komga server and API key (SPEC-APP.md §10).
FormCard.FormCardPage {
    id: page

    title: i18nc("@title", "Komga")

    FormCard.FormHeader {
        title: i18nc("@title:group", "Server")
    }

    FormCard.FormCard {
        FormCard.FormTextFieldDelegate {
            id: urlField

            label: i18nc("@label:textbox", "Server URL:")
            text: AppController.komgaUrl
            onAccepted: saveUrl.clicked()
        }

        FormCard.FormDelegateSeparator {
            above: urlField
            below: saveUrl
        }

        FormCard.FormButtonDelegate {
            id: saveUrl

            text: i18nc("@action:button", "Save URL")
            // Hints go here: placeholders in form fields read like values.
            description: i18n("For example %1", "https://komga.example.com")
            icon.name: "document-save"
            enabled: urlField.text !== AppController.komgaUrl
            onClicked: {
                const message = AppController.setKomgaUrl(urlField.text);
                urlMessage.text = message;
                urlMessage.visible = message !== "";
            }
        }

        FormCard.FormTextDelegate {
            id: urlMessage

            visible: false
            textItem.color: Kirigami.Theme.negativeTextColor
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "API Key")
    }

    FormCard.FormCard {
        FormCard.FormTextDelegate {
            text: i18n("Status")
            description: AppController.komgaState === "ready" ? i18n("Connected as %1", AppController.userEmail) : AppController.komgaMessage
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormPasswordFieldDelegate {
            id: keyField

            label: i18nc("@label:textbox", "New API key:")
        }

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Check and Save")
            description: i18n("Create one in Komga under My Account, API Keys")
            icon.name: "dialog-password"
            enabled: keyField.text !== ""
            onClicked: {
                keyMessage.text = i18n("Checking the key with Komga…");
                keyMessage.textItem.color = Kirigami.Theme.textColor;
                keyMessage.visible = true;
                AppController.checkAndSaveApiKey(keyField.text);
            }
        }

        FormCard.FormTextDelegate {
            id: keyMessage

            visible: false
        }
    }

    Connections {
        target: AppController

        function onApiKeyResult(ok: bool, message: string): void {
            keyMessage.text = message;
            keyMessage.textItem.color = ok ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.negativeTextColor;
            if (ok) {
                keyField.text = "";
            }
        }
    }
}
