// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import me.kodara.kobold

// Settings: known Kobos, their names, comic profiles and Komga sync (SPEC-APP.md §10,
// SPEC-KOBO-SYNC.md §8).
FormCard.FormCardPage {
    id: page

    title: i18nc("@title", "Kobos")

    // The Kobo of the last Komga sync action, whose card shows the outcome.
    property string lastSerial

    KomgaSignInDialog {
        id: signIn
        // Settings pages live in their own window: open there, not in the main window.
        parent: page.QQC2.Overlay.overlay
    }

    Kirigami.PromptDialog {
        id: resetDialog
        // Settings pages live in their own window: open there, not in the main window.
        parent: page.QQC2.Overlay.overlay

        property string serial
        property string koboName

        title: i18nc("@title:dialog", "Force Full Sync")
        subtitle: i18n("%1 will download every book again from Komga at its next sync.", resetDialog.koboName)
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Force Full Sync")
                icon.name: "edit-reset"
                onTriggered: {
                    page.lastSerial = resetDialog.serial;
                    AppController.runKoboSync(resetDialog.serial, "reset");
                    resetDialog.close();
                }
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Cancel")
                icon.name: "dialog-cancel"
                onTriggered: resetDialog.close()
            }
        ]
    }

    // One window for the books of a Kobo (SPEC-KOBO-BOOKS.md §6).
    property var booksWindow: null

    function manageBooks(serial) {
        AppController.koboBooks.open(serial);
        if (!page.booksWindow) {
            const component = Qt.createComponent("me.kodara.kobold", "KoboBooksWindow");
            if (component.status !== Component.Ready) {
                console.warn(component.errorString());
                return;
            }
            page.booksWindow = component.createObject(null);
        }
        page.booksWindow.show();
        page.booksWindow.raise();
        page.booksWindow.requestActivate();
    }

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing

        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Eject after sync")
            description: i18n("A sync that changed a Kobo ejects it, so that it indexes its books.")
            checked: AppController.ejectAfterSync
            onToggled: AppController.ejectAfterSync = checked
        }

        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Set series when a Kobo is connected")
            description: i18n("The Kobo groups books by series only once it has imported them: Kobold sets their series as soon as the Kobo is connected again, after a backup of its database.")
            checked: AppController.seriesOnConnect
            onToggled: AppController.seriesOnConnect = checked
        }

        FormCard.FormSwitchDelegate {
            text: i18nc("@option:check", "Show the steps to group new books by series")
            description: i18n("After a sync that copied books of a series, explains how to reconnect the Kobo so that they are grouped.")
            checked: AppController.showSeriesSteps
            onToggled: AppController.showSeriesSteps = checked
        }
    }

    Repeater {
        model: AppController.kobos

        delegate: ColumnLayout {
            id: kobo

            required property string serial
            required property string name
            required property string model
            required property bool connected
            required property string profile
            required property string profileSource
            required property bool koboSync
            required property string koboSyncMessage
            required property bool backupBeforeSync

            readonly property bool canChange: kobo.connected && !AppController.koboSyncBusy && AppController.komgaState === "ready"
            readonly property string disabledReason: !kobo.connected ? i18n("Connect the Kobo first.") : AppController.komgaState !== "ready" ? i18n("Komga is not ready.") : ""

            function run(action) {
                signIn.waiting = true;
                page.lastSerial = kobo.serial;
                AppController.runKoboSync(kobo.serial, action);
            }

            Layout.fillWidth: true
            spacing: 0

            FormCard.FormHeader {
                title: kobo.name
            }

            FormCard.FormCard {
                FormCard.FormTextDelegate {
                    text: kobo.model
                    description: kobo.connected ? i18n("Serial %1, connected", kobo.serial) : i18n("Serial %1, not connected", kobo.serial)
                }

                FormCard.FormDelegateSeparator {}

                FormCard.FormTextFieldDelegate {
                    id: nameField

                    label: i18nc("@label:textbox", "Name:")
                    text: kobo.name
                    onAccepted: renameButton.clicked()
                }

                FormCard.FormButtonDelegate {
                    id: renameButton

                    text: i18nc("@action:button", "Rename")
                    icon.name: "edit-rename"
                    enabled: nameField.text !== kobo.name
                    onClicked: {
                        const message = AppController.renameKobo(kobo.serial, nameField.text);
                        renameMessage.text = message;
                        renameMessage.visible = message !== "";
                    }
                }

                FormCard.FormTextDelegate {
                    id: renameMessage

                    visible: false
                    textItem.color: Kirigami.Theme.negativeTextColor
                }

                FormCard.FormDelegateSeparator {}

                FormCard.FormComboBoxDelegate {
                    text: i18nc("@label:listbox", "Comic profile:")
                    model: [i18nc("@item:inlistbox", "Automatic (from the model)")].concat(AppController.comicProfiles)
                    currentIndex: kobo.profileSource === "override" ? AppController.comicProfiles.indexOf(kobo.profile) + 1 : 0
                    description: kobo.profile === "" ? i18n("No profile: comics are skipped for this Kobo.") : i18n("Comics are converted with the KCC profile %1.", kobo.profile)
                    onActivated: index => AppController.setComicProfile(kobo.serial, index === 0 ? "" : AppController.comicProfiles[index - 1])
                }

                FormCard.FormDelegateSeparator {}

                FormCard.FormButtonDelegate {
                    objectName: "manageBooks"
                    text: i18nc("@action:button", "Manage Books on the Kobo…")
                    description: kobo.connected ? i18n("See every book of the Kobo, hide or show them, with a backup and a way to undo.") : i18n("Connect the Kobo first.")
                    icon.name: "view-list-details"
                    enabled: kobo.connected
                    onClicked: page.manageBooks(kobo.serial)
                }

                FormCard.FormSwitchDelegate {
                    text: i18nc("@option:check", "Back up the database before each sync")
                    description: i18n("Backups older than 90 days are removed; the five most recent are kept.")
                    checked: kobo.backupBeforeSync
                    onToggled: AppController.setBackupBeforeSync(kobo.serial, checked)
                }

                // Komga sync belongs to this Kobo: same card, after a separator.
                FormCard.FormDelegateSeparator {}

                FormCard.FormTextDelegate {
                    text: i18nc("@label Komga sync: state", "Komga sync: %1",
                                kobo.koboSync ? (kobo.koboSyncMessage !== "" ? kobo.koboSyncMessage : i18n("Installed")) : i18n("Not installed"))
                    textItem.color: kobo.koboSyncMessage !== "" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
                    description: i18n("EPUB books come from Komga over Wi-Fi; comics and PDF stay with Kobold. The selection does not apply to EPUB books.")
                }

                FormCard.FormDelegateSeparator {}

                FormCard.FormButtonDelegate {
                    visible: !kobo.koboSync
                    text: i18nc("@action:button", "Install")
                    description: kobo.disabledReason
                    icon.name: "list-add"
                    enabled: kobo.canChange
                    onClicked: kobo.run("install")
                }

                FormCard.FormButtonDelegate {
                    visible: kobo.koboSync
                    text: i18nc("@action:button", "Reinstall")
                    description: kobo.disabledReason
                    icon.name: "view-refresh"
                    enabled: kobo.canChange
                    onClicked: kobo.run("reinstall")
                }

                FormCard.FormButtonDelegate {
                    visible: kobo.koboSync
                    text: i18nc("@action:button", "Uninstall")
                    description: kobo.disabledReason
                    icon.name: "list-remove"
                    enabled: kobo.canChange
                    onClicked: kobo.run("uninstall")
                }

                FormCard.FormButtonDelegate {
                    visible: kobo.koboSync
                    text: i18nc("@action:button", "Change Key…")
                    description: i18n("Creates a new Komga key for this Kobo; the former one can then be deleted in Komga.")
                    icon.name: "document-encrypt"
                    enabled: !AppController.koboSyncBusy && AppController.komgaState === "ready"
                    onClicked: kobo.run("changeKey")
                }

                FormCard.FormButtonDelegate {
                    visible: kobo.koboSync
                    text: i18nc("@action:button", "Force Full Sync…")
                    description: i18n("The Kobo downloads every book again from Komga.")
                    icon.name: "edit-reset"
                    enabled: !AppController.koboSyncBusy && AppController.komgaState === "ready"
                    onClicked: {
                        resetDialog.serial = kobo.serial;
                        resetDialog.koboName = kobo.name;
                        resetDialog.open();
                    }
                }

                FormCard.FormTextDelegate {
                    visible: page.lastSerial === kobo.serial && (AppController.koboSyncBusy || AppController.koboSyncResult !== "")
                    text: AppController.koboSyncBusy ? i18n("Working…") : AppController.koboSyncResult
                    textItem.wrapMode: Text.WordWrap
                    textItem.color: AppController.koboSyncResultOk ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
                }
            }
        }
    }

    FormCard.FormCard {
        visible: AppController.kobos.count === 0
        Layout.topMargin: Kirigami.Units.largeSpacing

        FormCard.FormTextDelegate {
            text: i18n("No Kobo known yet. Plug one in, then use Look for Kobos in the toolbar.")
        }
    }
}
