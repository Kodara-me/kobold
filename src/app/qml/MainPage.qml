// SPDX-License-Identifier: MIT

import QtCore
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Main toolbar and the three panes separated by splitters (SPEC-APP.md §4).
Kirigami.Page {
    id: page

    signal settingsRequested(string moduleId)
    signal aboutRequested

    // The current Kobo pane, hidden by default on narrow windows.
    property bool koboPaneVisible: applicationWindow().width >= Kirigami.Units.gridUnit * 45

    readonly property bool canRun: AppController.currentKoboConnected && !AppController.busy && AppController.komgaState === "ready"

    padding: 0
    title: i18n("Kobold")

    titleDelegate: RowLayout {
        spacing: Kirigami.Units.mediumSpacing

        Kirigami.Icon {
            source: "tablet"
            implicitWidth: Kirigami.Units.iconSizes.smallMedium
            implicitHeight: Kirigami.Units.iconSizes.smallMedium
        }

        QQC2.ComboBox {
            id: koboCombo

            Layout.preferredWidth: Kirigami.Units.gridUnit * 12
            model: AppController.kobos
            textRole: "name"
            valueRole: "serial"
            enabled: count > 0 && !AppController.busy
            displayText: count === 0 ? i18n("No Kobo known yet") : AppController.currentKoboName
            // Depends on count so that it is evaluated again once the rows exist.
            currentIndex: count >= 0 ? indexOfValue(AppController.currentKobo) : -1
            onActivated: AppController.currentKobo = currentValue

            Accessible.name: i18n("Current Kobo")
            QQC2.ToolTip.text: i18n("The Kobo whose selection is shown and synced")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

            delegate: QQC2.ItemDelegate {
                required property string name
                required property bool connected
                required property string model
                required property int index

                width: koboCombo.width
                text: connected ? i18nc("@item Kobo name — model", "%1 — %2 (connected)", name, model) : i18nc("@item Kobo name — model", "%1 — %2", name, model)
                highlighted: koboCombo.highlightedIndex === index
            }
        }

        QQC2.ToolButton {
            icon.name: "view-refresh"
            text: i18nc("@action:button", "Look for Kobos")
            display: QQC2.AbstractButton.IconOnly
            enabled: !AppController.busy
            onClicked: AppController.reloadKobos()

            QQC2.ToolTip.text: text
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

        QQC2.Label {
            text: AppController.currentKobo === "" ? i18n("Plug in a Kobo")
                : AppController.currentKoboConnected ? i18n("Connected")
                : AppController.currentKoboMountable ? i18n("Plugged in, not mounted")
                : i18n("Not connected")
            color: AppController.currentKoboConnected ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
        }

        // A Kobo plugged in but not mounted (SPEC-MOUNT.md §6).
        QQC2.Button {
            visible: AppController.currentKoboMountable || (AppController.currentKobo === "" && AppController.unknownUnmountedCount > 0)
            text: i18nc("@action:button", "Mount")
            icon.name: "drive-removable-media"
            enabled: !AppController.busy
            onClicked: AppController.mountKobo(AppController.currentKoboMountable ? AppController.currentKobo : "")
        }

        // The connected Kobo can be ejected from here too (SPEC-MOUNT.md §6).
        QQC2.Button {
            visible: AppController.currentKoboConnected
            text: i18nc("@action:button", "Eject")
            icon.name: "media-eject"
            enabled: !AppController.busy
            onClicked: AppController.ejectCurrentKobo()

            QQC2.ToolTip.text: i18n("Unmount and power off the Kobo, so that it indexes its books")
            QQC2.ToolTip.visible: hovered
            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
        }

    }

    actions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Sync")
            icon.name: "folder-sync"
            enabled: page.canRun
            tooltip: i18n("Copy the selection to %1 and remove deselected books", AppController.currentKoboName)
            onTriggered: AppController.sync(false)
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Preview")
            icon.name: "document-preview"
            enabled: page.canRun
            tooltip: i18n("Show what a sync would change, without changing anything")
            onTriggered: AppController.sync(true)
        },
        Kirigami.Action {
            visible: AppController.parentalEnabled
            text: AppController.parentalLocked ? i18nc("@action:button", "Unlock") : i18nc("@action:button", "Lock")
            icon.name: AppController.parentalLocked ? "object-locked" : "object-unlocked"
            tooltip: AppController.parentalLocked ? i18n("Adult content is hidden; enter the PIN to show it") : i18n("Hide adult content again")
            displayHint: Kirigami.DisplayHint.IconOnly
            onTriggered: AppController.parentalLocked ? parentalPin.ask("unlock") : AppController.lockParental()
        },
        Kirigami.Action {
            text: i18nc("@action:inmenu", "Send Reading Progress")
            icon.name: "cloud-upload"
            enabled: page.canRun
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: AppController.pullProgress(false)
        },
        Kirigami.Action {
            text: i18nc("@action:inmenu", "Preview Reading Progress")
            icon.name: "document-preview"
            enabled: page.canRun
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: AppController.pullProgress(true)
        },
        Kirigami.Action {
            text: i18nc("@action:inmenu", "Sync All Connected Kobos")
            icon.name: "folder-sync"
            enabled: !AppController.busy && AppController.komgaState === "ready"
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: AppController.syncAll(false)
        },
        Kirigami.Action {
            text: i18nc("@action:inmenu", "Eject Kobo")
            icon.name: "media-eject"
            enabled: AppController.currentKoboConnected && !AppController.busy
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: AppController.ejectCurrentKobo()
        },
        Kirigami.Action {
            text: i18nc("@action:inmenu", "Reload Library")
            icon.name: "view-refresh"
            enabled: AppController.komgaState === "ready" && !AppController.libraryLoading
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onTriggered: AppController.reloadLibrary()
        },
        Kirigami.Action {
            text: i18nc("@action:inmenu", "Show Kobo Panel")
            icon.name: "sidebar-show-right"
            checkable: true
            checked: page.koboPaneVisible
            displayHint: Kirigami.DisplayHint.AlwaysHide
            onToggled: page.koboPaneVisible = checked
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Main Menu")
            icon.name: "application-menu"
            displayHint: Kirigami.DisplayHint.IconOnly

            Kirigami.Action {
                text: i18nc("@action:inmenu", "Settings…")
                icon.name: "configure"
                onTriggered: page.settingsRequested("")
            }
            Kirigami.Action {
                text: i18nc("@action:inmenu", "About Kobold")
                icon.name: "help-about"
                onTriggered: page.aboutRequested()
            }
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        KomgaMessage {
            Layout.fillWidth: true
            onSettingsRequested: page.settingsRequested("komga")
        }

        // The Komga sync endpoint of the current Kobo needs attention (SPEC-KOBO-SYNC.md §6).
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            position: Kirigami.InlineMessage.Position.Header
            type: Kirigami.MessageType.Warning
            visible: AppController.currentKoboSyncMessage !== ""
            text: AppController.currentKoboSyncMessage
            showCloseButton: true
            actions: [
                Kirigami.Action {
                    text: AppController.currentKoboSyncFix === "reinstall" ? i18nc("@action:button", "Reinstall") : i18nc("@action:button", "Change Key…")
                    icon.name: AppController.currentKoboSyncFix === "reinstall" ? "view-refresh" : "document-encrypt"
                    enabled: !AppController.koboSyncBusy && AppController.currentKoboConnected
                    onTriggered: {
                        signIn.waiting = true;
                        AppController.runKoboSync(AppController.currentKobo, AppController.currentKoboSyncFix);
                    }
                }
            ]
        }

        QQC2.SplitView {
            id: split

            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Sidebar {
                QQC2.SplitView.preferredWidth: Kirigami.Units.gridUnit * 15
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 10
            }

            ContentPane {
                QQC2.SplitView.fillWidth: true
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 16
            }

            KoboPane {
                visible: page.koboPaneVisible
                QQC2.SplitView.preferredWidth: Kirigami.Units.gridUnit * 17
                QQC2.SplitView.minimumWidth: Kirigami.Units.gridUnit * 12
                onSeriesActivated: seriesId => AppController.openItem("series", seriesId)
            }

            Component.onCompleted: {
                if (layout.splitState) {
                    split.restoreState(layout.splitState);
                }
            }
            Component.onDestruction: layout.splitState = split.saveState()
        }
    }

    KomgaSignInDialog {
        id: signIn
    }

    // Outcome of Mount and Eject.
    Connections {
        target: AppController

        function onMountResult(ok, message) {
            applicationWindow().showPassiveNotification(message, ok ? "short" : "long");
        }

        function onSeriesSetOnConnect(ok, message) {
            applicationWindow().showPassiveNotification(message, "long");
        }
    }

    ParentalPinDialog {
        id: parentalPin
    }

    SyncProgressDialog {}

    RemovalDialog {}

    // Pane widths kept between sessions.
    Settings {
        id: layout

        category: "MainWindowLayout"
        property var splitState
    }
}
