// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Trailing pane: the current Kobo's selection (SPEC-APP.md §7).
QQC2.Pane {
    id: pane

    signal seriesActivated(string seriesId)

    padding: 0

    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
    }

    QQC2.SplitView {
        anchors.fill: parent
        orientation: Qt.Vertical

        // Selection of the current Kobo.
        ColumnLayout {
            QQC2.SplitView.fillHeight: true
            QQC2.SplitView.minimumHeight: Kirigami.Units.gridUnit * 8
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.largeSpacing
                spacing: 0

                Kirigami.Heading {
                    Layout.fillWidth: true
                    level: 3
                    text: AppController.currentKoboName || i18n("No Kobo")
                    elide: Text.ElideRight
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: AppController.currentKobo === "" ? i18n("Plug in a Kobo to start.") : i18np("1 book selected", "%1 books selected", AppController.selectionCount)
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }

                // Adult items of the selection, left out while locked (SPEC-PARENTAL.md §4).
                QQC2.Label {
                    Layout.fillWidth: true
                    visible: AppController.hiddenSelectionCount > 0
                    text: i18np("1 item hidden", "%1 items hidden", AppController.hiddenSelectionCount)
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }
            }

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                type: Kirigami.MessageType.Error
                visible: AppController.selectionError !== ""
                text: AppController.selectionError
            }

            QQC2.ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

                ListView {
                    id: selection

                    clip: true
                    model: AppController.selectionGroups

                    header: Kirigami.ListSectionHeader {
                        width: ListView.view.width
                        text: i18nc("@title:group", "Followed")
                        visible: selection.count > 0
                        height: visible ? implicitHeight : 0
                    }

                    delegate: SelectionRow {
                        required property string kind
                        required property string kindLabel
                        required property string id
                        required property string name
                        required property string volumes
                        required property int count

                        width: ListView.view.width
                        title: name
                        details: [kindLabel, volumes !== "" ? i18n("volumes %1", volumes) : "", count >= 0 ? i18np("1 book", "%1 books", count) : ""].filter(part => part !== "").join(" · ")
                        cover: kind === "series" ? "image://komga/series/" + id + AppController.coverStamp : ""
                        removeText: i18nc("@action:button", "Stop Following")
                        onClicked: if (kind === "series") pane.seriesActivated(id)
                        onRemoveRequested: AppController.removeFromSelection(kind, id)
                    }

                    footer: ColumnLayout {
                        width: ListView.view.width
                        spacing: 0

                        Kirigami.ListSectionHeader {
                            Layout.fillWidth: true
                            text: i18nc("@title:group", "Books")
                            visible: AppController.selectionBooks.count > 0
                        }

                        Repeater {
                            model: AppController.selectionBooks

                            delegate: SelectionRow {
                                required property string id
                                required property string format
                                required title

                                Layout.fillWidth: true
                                details: format
                                cover: "image://komga/book/" + id + AppController.coverStamp
                                removeText: i18nc("@action:button", "Remove from Selection")
                                onRemoveRequested: AppController.removeFromSelection("book", id)
                            }
                        }

                        // Files from the computer (SPEC-LOCAL.md §6).
                        RowLayout {
                            Layout.fillWidth: true
                            visible: AppController.currentKobo !== ""
                            spacing: 0

                            Kirigami.ListSectionHeader {
                                Layout.fillWidth: true
                                text: i18nc("@title:group", "Local Files")
                            }

                            QQC2.ToolButton {
                                icon.name: "list-add"
                                text: i18nc("@action:button", "Add Files…")
                                display: QQC2.AbstractButton.IconOnly
                                onClicked: fileDialog.open()

                                QQC2.ToolTip.text: text
                                QQC2.ToolTip.visible: hovered
                                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                            }
                        }

                        Repeater {
                            model: AppController.localFiles

                            delegate: SelectionRow {
                                required property string path
                                required property string name
                                required property bool missing

                                Layout.fillWidth: true
                                title: name
                                details: missing ? i18n("Missing on the computer") : path
                                removeText: i18nc("@action:button", "Remove from Selection")
                                onRemoveRequested: AppController.removeLocalFile(path)
                            }
                        }

                        // Books the user kept although sync would remove them (SPEC-UNSYNC.md §4).
                        Kirigami.ListSectionHeader {
                            Layout.fillWidth: true
                            text: i18nc("@title:group", "Kept on the Kobo")
                            visible: AppController.keptBooks.count > 0
                        }

                        Repeater {
                            model: AppController.keptBooks

                            delegate: SelectionRow {
                                required property string bookId
                                required property string label
                                required property string reason

                                Layout.fillWidth: true
                                title: label
                                details: reason
                                removeText: i18nc("@action:button", "Remove Now")
                                onRemoveRequested: {
                                    removeKept.bookId = bookId;
                                    removeKept.label = label;
                                    removeKept.open();
                                }
                            }
                        }

                        Kirigami.PlaceholderMessage {
                            Layout.fillWidth: true
                            Layout.margins: Kirigami.Units.largeSpacing
                            visible: AppController.currentKobo !== "" && selection.count === 0 && AppController.selectionBooks.count === 0 && AppController.localFiles.count === 0
                            text: i18n("Nothing selected yet")
                            explanation: i18n("Check books or follow series in the content pane.")
                        }
                    }
                }
            }
        }
    }

    // A selection entry with a visible remove button (HIG: inline remove controls).
    FileDialog {
        id: fileDialog

        title: i18nc("@title:window", "Add Files to the Selection")
        fileMode: FileDialog.OpenFiles
        nameFilters: [i18n("Books (*.epub *.pdf *.cbz *.cbr)")]
        onAccepted: {
            const message = AppController.addLocalFiles(selectedFiles);
            if (message !== "") {
                applicationWindow().showPassiveNotification(message);
            }
        }
    }

    Kirigami.PromptDialog {
        id: removeKept

        property string bookId
        property string label

        title: i18nc("@title:dialog", "Remove from the Kobo?")
        subtitle: i18n("%1 will be deleted from the Kobo now.", removeKept.label)
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Remove")
                icon.name: "edit-delete"
                onTriggered: {
                    const message = AppController.removeKeptBook(removeKept.bookId);
                    removeKept.close();
                    if (message !== "") {
                        applicationWindow().showPassiveNotification(message);
                    }
                }
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Cancel")
                icon.name: "dialog-cancel"
                onTriggered: removeKept.close()
            }
        ]
    }

    component SelectionRow: QQC2.ItemDelegate {
        id: row

        property string title
        property string details
        property string cover
        property string removeText
        signal removeRequested

        contentItem: RowLayout {
            spacing: Kirigami.Units.largeSpacing

            Image {
                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                Layout.preferredHeight: Kirigami.Units.iconSizes.large
                visible: row.cover !== ""
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                sourceSize.height: Kirigami.Units.iconSizes.large
                source: row.cover
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                QQC2.Label {
                    Layout.fillWidth: true
                    text: row.title
                    elide: Text.ElideRight
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: row.details
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    elide: Text.ElideRight
                }
            }

            QQC2.ToolButton {
                icon.name: "list-remove"
                text: row.removeText
                display: QQC2.AbstractButton.IconOnly
                onClicked: row.removeRequested()

                QQC2.ToolTip.text: text
                QQC2.ToolTip.visible: hovered
                QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
            }
        }
    }
}
