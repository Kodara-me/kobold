// SPDX-License-Identifier: MIT

import QtQuick
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Explains why Komga is not usable and leads to the settings (SPEC-APP.md §4).
Kirigami.InlineMessage {
    id: message

    signal settingsRequested

    position: Kirigami.InlineMessage.Position.Header
    // Without a Komga key, shelves alone are enough: no warning then (SPEC-SHELF.md).
    visible: AppController.komgaState !== "ready" && !(AppController.komgaState === "noKey" && AppController.hasShelves)
    type: AppController.komgaState === "connecting" ? Kirigami.MessageType.Information : Kirigami.MessageType.Warning
    text: AppController.komgaMessage
    showCloseButton: false
    actions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Open Settings…")
            icon.name: "configure"
            visible: AppController.komgaState !== "connecting"
            onTriggered: message.settingsRequested()
        }
    ]
}
