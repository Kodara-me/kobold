// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import me.kodara.kobold

// Settings: shelves, folders of books outside Komga (SPEC-SHELF.md §2, §3).
FormCard.FormCardPage {
    id: page

    title: i18nc("@title", "Shelves")

    FolderDialog {
        id: folderDialog

        title: i18nc("@title:window", "Choose the Shelf's Folder")
        onAccepted: {
            const message = AppController.addShelf(nameField.text, selectedFolder);
            addMessage.text = message;
            if (message === "") {
                nameField.text = "";
            }
        }
    }

    // No shelf yet: one click makes a folder for Kobold (SPEC-SHELF.md §2.1).
    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing
        visible: AppController.shelves.count === 0

        FormCard.FormButtonDelegate {
            objectName: "createKoboldLibrary"
            text: i18nc("@action:button", "Create a Kobold Library")
            description: i18n("Creates the folder \"%1\" in %2, with its own icon, and adds it as a shelf. Put books there, then rescan.",
                              i18nc("@info folder and shelf name", "Kobold Library"), AppController.koboldLibraryParent())
            icon.source: "qrc:/qt/qml/me/kodara/kobold/icons/folder-kobold.svg"
            enabled: !AppController.shelfScanning
            onClicked: {
                const message = AppController.createKoboldLibrary();
                addMessage.text = message;
            }
        }
    }

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing

        FormCard.FormTextDelegate {
            text: i18n("A shelf is a folder of books outside Komga, on this computer or on a network share mounted by the system. Kobold reads it and never changes it.")
            textItem.wrapMode: Text.WordWrap
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormTextFieldDelegate {
            id: nameField

            // Optional: the folder's name otherwise. No placeholder, which read like a value.
            label: i18nc("@label:textbox", "Name (optional):")
        }

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Add Shelf…")
            description: i18n("Choose the folder, then Kobold scans it. Without a name, the shelf takes the folder's name.")
            icon.name: "folder-add"
            enabled: !AppController.shelfScanning
            onClicked: folderDialog.open()
        }

        FormCard.FormTextDelegate {
            id: addMessage

            visible: text !== ""
            textItem.color: Kirigami.Theme.negativeTextColor
        }

        FormCard.FormTextDelegate {
            visible: AppController.shelfScanStatus !== ""
            text: AppController.shelfScanStatus
            textItem.wrapMode: Text.WordWrap
        }
    }

    Repeater {
        model: AppController.shelves

        delegate: ColumnLayout {
            id: shelf

            required property var id
            required property string name
            required property string path
            required property int books
            required property string lastScan
            required property bool available

            Layout.fillWidth: true
            spacing: 0

            FormCard.FormHeader {
                title: shelf.name
            }

            FormCard.FormCard {
                FormCard.FormTextDelegate {
                    text: shelf.path
                    description: !shelf.available ? i18n("Unavailable: is the share mounted?")
                                                  : shelf.lastScan === "" ? i18np("1 book", "%1 books", shelf.books)
                                                  : i18n("%1, scanned %2", i18np("1 book", "%1 books", shelf.books), shelf.lastScan)
                    descriptionItem.color: shelf.available ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.negativeTextColor
                }

                FormCard.FormDelegateSeparator {}

                FormCard.FormButtonDelegate {
                    text: i18nc("@action:button", "Rescan")
                    icon.name: "view-refresh"
                    enabled: !AppController.shelfScanning
                    onClicked: AppController.scanShelf(shelf.id)
                }

                FormCard.FormTextFieldDelegate {
                    id: renameField

                    label: i18nc("@label:textbox", "Name:")
                    text: shelf.name
                    onAccepted: renameButton.clicked()
                }

                FormCard.FormButtonDelegate {
                    id: renameButton

                    text: i18nc("@action:button", "Rename")
                    icon.name: "edit-rename"
                    enabled: renameField.text.trim() !== "" && renameField.text !== shelf.name
                    onClicked: renameMessage.text = AppController.renameShelf(shelf.id, renameField.text)
                }

                FormCard.FormTextDelegate {
                    id: renameMessage

                    visible: text !== ""
                    textItem.color: Kirigami.Theme.negativeTextColor
                }

                FormCard.FormButtonDelegate {
                    text: i18nc("@action:button", "Remove Shelf")
                    description: i18n("Kobold forgets this shelf; its files are not touched.")
                    icon.name: "list-remove"
                    enabled: !AppController.shelfScanning
                    onClicked: AppController.removeShelf(shelf.id)
                }
            }
        }
    }

    // Edits move between computers as a file (SPEC-SHELF.md §5).
    FileDialog {
        id: exportDialog

        title: i18nc("@title:window", "Export the Shelf Edits")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [i18n("Kobold shelf edits (*.json)")]
        onAccepted: {
            const message = AppController.shelfEditor.exportEdits(selectedFile);
            editsMessage.text = message === "" ? i18n("Edits exported.") : message;
        }
    }

    FileDialog {
        id: importDialog

        title: i18nc("@title:window", "Import Shelf Edits")
        nameFilters: [i18n("Kobold shelf edits (*.json)")]
        onAccepted: {
            const message = AppController.shelfEditor.importEdits(selectedFile);
            editsMessage.text = message === "" ? AppController.shelfEditor.message : message;
        }
    }

    FormCard.FormHeader {
        visible: AppController.shelves.count > 0
        title: i18nc("@title:group", "Edits and Online Lookup")
    }

    FormCard.FormCard {
        visible: AppController.shelves.count > 0

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Export Edits…")
            description: i18n("Your edits and the values accepted online, with their covers, in one file for another computer.")
            icon.name: "document-export"
            onClicked: exportDialog.open()
        }

        FormCard.FormButtonDelegate {
            text: i18nc("@action:button", "Import Edits…")
            description: i18n("Shelves of the same name get the edits; books are found even when they moved.")
            icon.name: "document-import"
            onClicked: importDialog.open()
        }

        FormCard.FormTextDelegate {
            id: editsMessage

            visible: text !== ""
            textItem.wrapMode: Text.WordWrap
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormPasswordFieldDelegate {
            id: comicVineKey

            label: i18nc("@label:textbox", "ComicVine API key:")
            onAccepted: saveKey.clicked()
        }

        FormCard.FormButtonDelegate {
            id: saveKey

            text: comicVineKey.text.trim() === "" && AppController.shelfEditor.hasComicVineKey ? i18nc("@action:button", "Remove the Key")
                                                                                             : i18nc("@action:button", "Save the Key")
            description: AppController.shelfEditor.hasComicVineKey ? i18n("A key is stored in the keychain. Comics are looked up on ComicVine.")
                                                                   : i18n("Optional: comics are looked up on ComicVine with your free key (comicvine.gamespot.com/api); without it, on the book sources.")
            icon.name: "document-encrypt"
            enabled: comicVineKey.text.trim() !== "" || AppController.shelfEditor.hasComicVineKey
            onClicked: {
                AppController.shelfEditor.setComicVineKey(comicVineKey.text);
                comicVineKey.text = "";
            }
        }
    }

    Component.onCompleted: AppController.shelfEditor.loadComicVineKey()
}
