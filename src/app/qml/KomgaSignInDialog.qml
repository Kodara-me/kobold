// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Asks once for the Komga email and password when the API key cannot create the Kobo's key
// (SPEC-KOBO-SYNC.md §4.2, §8). Nothing typed here is kept.
Kirigami.Dialog {
    id: dialog

    // Set by the page that started the action, so that only its window asks.
    property bool waiting: false

    title: i18nc("@title:dialog", "Sign In to Komga")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 22
    standardButtons: Kirigami.Dialog.NoButton

    function submit() {
        AppController.provideKomgaCredentials(email.text, password.text);
        password.text = "";
        dialog.waiting = false;
        dialog.close();
    }

    onRejected: {
        password.text = "";
        if (dialog.waiting) {
            dialog.waiting = false;
            AppController.cancelKomgaCredentials();
        }
    }

    Connections {
        target: AppController

        function onKomgaCredentialsNeeded() {
            if (dialog.waiting) {
                dialog.open();
                email.forceActiveFocus();
            }
        }

        function onKoboSyncChanged() {
            if (!AppController.koboSyncBusy) {
                dialog.waiting = false;
            }
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.mediumSpacing

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
            wrapMode: Text.WordWrap
            text: i18n("Komga needs your email and password to create the Kobo's key. They are used once and not kept.")
        }

        QQC2.Label {
            text: i18nc("@label:textbox", "Email:")
        }

        QQC2.TextField {
            id: email

            Layout.fillWidth: true
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            text: AppController.userEmail
            onAccepted: password.forceActiveFocus()
        }

        QQC2.Label {
            text: i18nc("@label:textbox", "Password:")
        }

        Kirigami.PasswordField {
            id: password

            Layout.fillWidth: true
            onAccepted: dialog.submit()
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Sign In")
            icon.name: "dialog-ok"
            enabled: email.text !== "" && password.text !== ""
            onTriggered: dialog.submit()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Cancel")
            icon.name: "dialog-cancel"
            onTriggered: dialog.reject()
        }
    ]
}
