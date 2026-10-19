// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Edits a shelf book, several books (shared fields) or a series (SPEC-SHELF.md §4.4). Each value
// shows where it comes from; reverting a manual value brings the next source back.
Kirigami.Dialog {
    id: dialog

    objectName: "shelfEditorDialog"

    readonly property var editor: AppController.shelfEditor
    // Field → text typed and not saved yet.
    property var edits: ({})

    // The lookup opens next to this dialog, not inside it.
    signal findMetadata()

    function openBooks(ids) {
        const message = editor.openBooks(ids);
        if (message === "") {
            dialog.edits = {};
            dialog.open();
        }
        return message;
    }

    function openSeries(id) {
        const message = editor.openSeries(id);
        if (message === "") {
            dialog.edits = {};
            dialog.open();
        }
        return message;
    }

    function save() {
        if (Object.keys(dialog.edits).length === 0) {
            return true;
        }
        const message = editor.save(dialog.edits);
        error.text = message;
        if (message === "") {
            dialog.edits = {};
        }
        return message === "";
    }

    title: editor.kind === "series" ? i18nc("@title:dialog", "Edit Series") : editor.kind === "books" ? i18nc("@title:dialog", "Edit %1", editor.title)
                                                                                                    : i18nc("@title:dialog", "Edit Book")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 36
    standardButtons: Kirigami.Dialog.NoButton

    FileDialog {
        id: imageDialog

        title: i18nc("@title:window", "Choose the Cover")
        nameFilters: [i18n("Images (*.jpg *.jpeg *.png *.webp)")]
        onAccepted: error.text = dialog.editor.setCoverImage(selectedFile)
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            id: error

            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: text !== ""
            showCloseButton: true
        }

        RowLayout {
            spacing: Kirigami.Units.largeSpacing

            // Cover and where it comes from (§4.4): the file, another page, an image.
            ColumnLayout {
                visible: dialog.editor.kind !== "books"
                Layout.alignment: Qt.AlignTop
                Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                spacing: Kirigami.Units.smallSpacing

                Image {
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 9
                    Layout.preferredHeight: Kirigami.Units.gridUnit * 13
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: false
                    source: dialog.editor.coverUrl
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Kirigami.Theme.disabledTextColor
                    text: dialog.editor.coverChoice === "image" ? i18n("Cover: a chosen image")
                        : dialog.editor.coverChoice === "page" ? i18n("Cover: page %1", dialog.editor.coverPage + 1)
                                                               : i18n("Cover: from the file")
                }

                RowLayout {
                    visible: dialog.editor.pageCount > 1

                    QQC2.SpinBox {
                        id: page

                        from: 1
                        to: dialog.editor.pageCount
                        value: Math.max(1, dialog.editor.coverPage + 1)
                    }

                    QQC2.Button {
                        text: i18nc("@action:button", "Use Page")
                        onClicked: error.text = dialog.editor.setCoverPage(page.value - 1)
                    }
                }

                QQC2.Button {
                    Layout.fillWidth: true
                    text: i18nc("@action:button", "Image…")
                    icon.name: "insert-image"
                    onClicked: imageDialog.open()
                }

                QQC2.Button {
                    Layout.fillWidth: true
                    visible: dialog.editor.coverChoice !== "file"
                    text: dialog.editor.kind === "series" ? i18nc("@action:button", "First Book's Cover") : i18nc("@action:button", "File's Cover")
                    icon.name: "edit-undo"
                    onClicked: error.text = dialog.editor.resetCover()
                }
            }

            QQC2.ScrollView {
                id: scroll

                Layout.fillWidth: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 24
                Layout.minimumWidth: Kirigami.Units.gridUnit * 20
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                ColumnLayout {
                    width: scroll.availableWidth
                    spacing: Kirigami.Units.smallSpacing

                    Repeater {
                        model: dialog.editor.fields

                        delegate: ColumnLayout {
                            id: row

                            required property string field
                            required property string label
                            required property string value
                            required property string sourceLabel
                            required property bool manual
                            required property bool mixed
                            required property bool multiline

                            Layout.fillWidth: true
                            spacing: 0

                            RowLayout {
                                Layout.fillWidth: true

                                QQC2.Label {
                                    text: row.label
                                }

                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: row.sourceLabel === "" ? "" : "(" + row.sourceLabel + ")"
                                    color: row.manual ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.disabledTextColor
                                    font: Kirigami.Theme.smallFont
                                }

                                QQC2.ToolButton {
                                    visible: row.manual
                                    icon.name: "edit-undo"
                                    text: i18nc("@action:button", "Revert")
                                    display: QQC2.AbstractButton.IconOnly
                                    onClicked: error.text = dialog.editor.clear(row.field)
                                    QQC2.ToolTip.text: i18nc("@info:tooltip", "Clear the manual value: the next source shows again")
                                    QQC2.ToolTip.visible: hovered
                                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                                }
                            }

                            QQC2.TextField {
                                Layout.fillWidth: true
                                visible: !row.multiline
                                text: row.value
                                placeholderText: row.mixed ? i18n("Several values") : ""
                                onTextEdited: {
                                    dialog.edits[row.field] = text;
                                    dialog.edits = dialog.edits;
                                }
                            }

                            QQC2.TextArea {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Kirigami.Units.gridUnit * 5
                                visible: row.multiline
                                wrapMode: TextEdit.Wrap
                                text: row.value
                                onTextChanged: {
                                    if (activeFocus) {
                                        dialog.edits[row.field] = text;
                                        dialog.edits = dialog.edits;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Find Metadata…")
            icon.name: "edit-find"
            visible: dialog.editor.kind !== "books"
            onTriggered: {
                if (dialog.save()) {
                    dialog.findMetadata();
                }
            }
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Edit Every Volume…")
            icon.name: "edit-select-all"
            visible: dialog.editor.kind === "series"
            onTriggered: {
                if (dialog.save()) {
                    error.text = dialog.editor.openBooks(AppController.seriesBookIds(dialog.editor.ids[0]));
                    dialog.edits = {};
                }
            }
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Save")
            icon.name: "document-save"
            enabled: Object.keys(dialog.edits).length > 0
            onTriggered: dialog.save()
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Close")
            icon.name: "dialog-close"
            onTriggered: {
                dialog.edits = {};
                dialog.close();
            }
        }
    ]
}
