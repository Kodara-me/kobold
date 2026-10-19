// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Blocks the window while a sync, a preview or a progress transfer runs, then shows how it
// ended (SPEC-APP.md §7): the errors, a preview's list of changes, and after a sync that copied
// books of a series, the steps that group them on the Kobo (SPEC-KOBO-BOOKS.md §3.2).
Kirigami.Dialog {
    id: dialog

    objectName: "syncProgressDialog"

    readonly property bool running: AppController.busy
    readonly property bool succeeded: !running && AppController.failures.length === 0 && AppController.lastExitCode === 0
    readonly property bool showSteps: !running && !AppController.lastDryRun && AppController.lastSeriesPending > 0 && AppController.showSeriesSteps

    title: AppController.operationTitle
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 30
    closePolicy: running ? QQC2.Popup.NoAutoClose : QQC2.Popup.CloseOnEscape
    showCloseButton: false
    standardButtons: Kirigami.Dialog.NoButton

    Connections {
        target: AppController

        function onBusyChanged() {
            if (AppController.busy) {
                hideSteps.checked = false;
                details.checked = false;
                dialog.open();
            } else {
                // A preview is its list of changes; a failure needs its lines.
                details.checked = AppController.lastDryRun || AppController.failures.length > 0;
            }
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.mediumSpacing

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 30
            visible: dialog.running
            elide: Text.ElideMiddle
            text: AppController.currentStep !== "" ? AppController.currentStep : i18n("Starting…")
        }

        QQC2.ProgressBar {
            Layout.fillWidth: true
            visible: dialog.running
            indeterminate: AppController.progressTotal === 0
            from: 0
            to: Math.max(1, AppController.progressTotal)
            value: AppController.progressDone
        }

        QQC2.Label {
            Layout.fillWidth: true
            visible: dialog.running && AppController.progressTotal > 0
            color: Kirigami.Theme.disabledTextColor
            text: i18nc("@info progress", "Step %1 of %2", Math.min(AppController.progressDone + 1, AppController.progressTotal), AppController.progressTotal)
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !dialog.running && AppController.failures.length > 0
            type: Kirigami.MessageType.Error
            text: AppController.failures.join("\n")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.succeeded && !AppController.lastDryRun
            type: Kirigami.MessageType.Positive
            text: AppController.lastSyncWrote && !dialog.showSteps && !AppController.ejectAfterSync
                  ? i18n("Sync finished. Eject the Kobo so that it indexes the changes.")
                  : i18n("Finished.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: dialog.succeeded && AppController.lastDryRun
            type: Kirigami.MessageType.Positive
            text: i18n("Preview finished. Nothing was changed on the Kobo.")
        }

        // The Kobo groups books by series only once it has imported them, after the eject.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 30
            visible: dialog.showSteps
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                Layout.fillWidth: true
                level: 4
                wrapMode: Text.Wrap
                text: i18n("One more step to group the new books by series")
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: Kirigami.Theme.disabledTextColor
                text: i18n("The Kobo sorts books into series only after adding them to its library, which it does once ejected.")
            }

            RowLayout {
                Layout.fillWidth: true

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: AppController.ejectAfterSync ? i18n("1. Kobold ejects the Kobo. It adds the new books to its library.")
                                                       : i18n("1. Eject the Kobo. It adds the new books to its library.")
                }

                QQC2.Button {
                    visible: !AppController.ejectAfterSync && AppController.lastSyncWrote
                    text: i18nc("@action:button", "Eject Now")
                    icon.name: "media-eject"
                    onClicked: AppController.ejectCurrentKobo()
                }
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: i18n("2. When it has finished, connect it again.")
            }

            QQC2.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: AppController.seriesOnConnect ? i18n("3. Kobold then sets their series by itself. Eject the Kobo once more: the series are grouped.")
                                                    : i18n("3. Sync it again, then eject it once more: the series are grouped.")
            }

            QQC2.CheckBox {
                id: hideSteps

                text: i18nc("@option:check", "Do not show these steps again")
                onToggled: AppController.showSeriesSteps = !checked
            }
        }

        // Every line of the operation, folded by default; open for a preview (its result) or a
        // failure.
        QQC2.Button {
            id: details

            checkable: true
            flat: true
            icon.name: checked ? "arrow-down" : "arrow-right"
            text: i18nc("@action:button", "Details")
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 14
            visible: details.checked
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                id: log

                model: AppController.log
                clip: true
                onCountChanged: positionViewAtEnd()

                delegate: QQC2.Label {
                    required property string kind
                    required property string message

                    width: ListView.view.width
                    wrapMode: Text.Wrap
                    font: Kirigami.Theme.fixedWidthFont
                    color: kind === "error" ? Kirigami.Theme.negativeTextColor : kind === "warning" ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.textColor
                    text: kind === "header" ? "== " + message : message
                }
            }
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Close")
            icon.name: "dialog-close"
            enabled: !dialog.running
            onTriggered: dialog.close()
        }
    ]
}
