// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Asks for the parental PIN (SPEC-PARENTAL.md §3, §4): to unlock, to turn parental control
// on (typed twice) or off.
Kirigami.Dialog {
    id: dialog

    // "unlock", "enable" or "disable".
    property string mode: "unlock"
    property bool waiting: false

    function ask(newMode) {
        dialog.mode = newMode;
        pin.text = "";
        confirmation.text = "";
        message.text = "";
        dialog.open();
        pin.forceActiveFocus();
    }

    function submit() {
        if (dialog.mode === "enable" && pin.text !== confirmation.text) {
            message.text = i18n("The two PINs differ.");
            return;
        }
        dialog.waiting = true;
        if (dialog.mode === "unlock") {
            AppController.unlockParental(pin.text);
        } else if (dialog.mode === "enable") {
            AppController.enableParental(pin.text);
        } else {
            AppController.disableParental(pin.text);
        }
    }

    title: dialog.mode === "enable" ? i18nc("@title:dialog", "Choose a Parental PIN")
         : dialog.mode === "disable" ? i18nc("@title:dialog", "Turn Off Parental Control")
         : i18nc("@title:dialog", "Unlock Adult Content")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 20
    standardButtons: Kirigami.Dialog.NoButton
    onClosed: {
        pin.text = "";
        confirmation.text = "";
    }

    Connections {
        target: AppController

        function onParentalResult(ok, text) {
            if (!dialog.waiting) {
                return;
            }
            dialog.waiting = false;
            if (ok) {
                dialog.close();
            } else {
                message.text = text;
                pin.text = "";
                pin.forceActiveFocus();
            }
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.mediumSpacing

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 20
            wrapMode: Text.WordWrap
            text: dialog.mode === "enable" ? i18n("The PIN has 4 to 8 digits. It is needed to show adult content and to change these settings.")
                : i18n("Enter the parental PIN.")
        }

        Kirigami.PasswordField {
            id: pin

            Layout.fillWidth: true
            placeholderText: i18nc("@info:placeholder", "PIN")
            inputMethodHints: Qt.ImhDigitsOnly
            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
            onAccepted: dialog.mode === "enable" ? confirmation.forceActiveFocus() : dialog.submit()
        }

        Kirigami.PasswordField {
            id: confirmation

            Layout.fillWidth: true
            visible: dialog.mode === "enable"
            placeholderText: i18nc("@info:placeholder", "PIN again")
            inputMethodHints: Qt.ImhDigitsOnly
            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
            onAccepted: dialog.submit()
        }

        QQC2.Label {
            id: message

            Layout.fillWidth: true
            visible: text !== ""
            wrapMode: Text.WordWrap
            color: Kirigami.Theme.negativeTextColor
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: dialog.mode === "enable" ? i18nc("@action:button", "Turn On")
                : dialog.mode === "disable" ? i18nc("@action:button", "Turn Off")
                : i18nc("@action:button", "Unlock")
            icon.name: dialog.mode === "disable" ? "object-unlocked" : dialog.mode === "enable" ? "object-locked" : "object-unlocked"
            enabled: !dialog.waiting && pin.text.length >= 4
            onTriggered: dialog.submit()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Cancel")
            icon.name: "dialog-cancel"
            onTriggered: dialog.close()
        }
    ]
}
