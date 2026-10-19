// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigami.delegates as KD
import me.kodara.kobold

// Center pane: the chosen series, collection or read list and its books (SPEC-APP.md §6).
QQC2.Pane {
    id: pane

    readonly property var item: AppController.currentItem
    readonly property bool hasItem: item.kind !== ""
    readonly property bool canSelect: AppController.currentKobo !== ""
    // Shelf series and books can be edited (SPEC-SHELF.md §4.4).
    readonly property bool onShelf: (item.id ?? "").startsWith("shelf:")

    padding: 0
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
    }

    function report(message: string): void {
        error.text = message;
        error.visible = message !== "";
    }

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        visible: !pane.hasItem
        icon.name: "file-library-symbolic"
        text: i18n("Choose a series in the sidebar")
        explanation: i18n("Check books, or follow the whole series, to put them on the current Kobo.")
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        visible: pane.hasItem

        // Header: what is shown and how it is selected.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            Image {
                visible: pane.item.cover !== undefined
                Layout.preferredWidth: Kirigami.Units.gridUnit * 4
                Layout.preferredHeight: Kirigami.Units.gridUnit * 6
                Layout.alignment: Qt.AlignTop
                asynchronous: true
                fillMode: Image.PreserveAspectFit
                horizontalAlignment: Image.AlignLeft
                verticalAlignment: Image.AlignTop
                sourceSize.height: Kirigami.Units.gridUnit * 6
                source: (pane.item.cover ?? "") === "" ? "" : pane.item.cover + AppController.coverStamp
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Kirigami.Units.smallSpacing

                // Title and details: no space between them (HIG spacing).
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    Kirigami.Heading {
                        Layout.fillWidth: true
                        level: 2
                        text: pane.item.title ?? ""
                        wrapMode: Text.Wrap
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: pane.item.details ?? ""
                        color: Kirigami.Theme.disabledTextColor
                        wrapMode: Text.Wrap
                    }
                }

                RowLayout {
                    visible: pane.onShelf
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Button {
                        text: i18nc("@action:button", "Edit Series…")
                        icon.name: "document-edit"
                        onClicked: pane.report(editor.openSeries(pane.item.id))
                    }

                    QQC2.Button {
                        text: i18nc("@action:button", "Find Metadata…")
                        icon.name: "edit-find"
                        onClicked: {
                            const message = AppController.shelfEditor.openSeries(pane.item.id);
                            pane.report(message);
                            if (message === "") {
                                lookup.start();
                            }
                        }
                    }
                }

                QQC2.Switch {
                    id: followSwitch

                    text: pane.item.kind === "series" ? i18nc("@option:check", "Follow this series on %1", AppController.currentKoboName)
                                                      : i18nc("@option:check", "Follow on %1", AppController.currentKoboName)
                    checked: pane.item.followed ?? false
                    enabled: pane.canSelect
                    onToggled: pane.report(AppController.setCurrentItemFollowed(checked, volumesField.text))
                }

                RowLayout {
                    visible: pane.item.kind === "series"
                    enabled: pane.canSelect
                    spacing: Kirigami.Units.smallSpacing

                    QQC2.Label {
                        text: i18nc("@label:textbox", "Volumes:")
                    }

                    QQC2.TextField {
                        id: volumesField

                        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                        text: pane.item.volumes ?? ""
                        placeholderText: i18n("All")
                        color: AppController.checkVolumes(text) === "" ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
                        onAccepted: applyButton.clicked()

                        QQC2.ToolTip.text: i18n("Leave empty for every volume, or enter a range such as 1-10 or 1-3,7,10-")
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    }

                    QQC2.Button {
                        id: applyButton

                        text: i18nc("@action:button", "Apply")
                        icon.name: "dialog-ok-apply"
                        enabled: AppController.checkVolumes(volumesField.text) === "" && (volumesField.text !== (pane.item.volumes ?? "") || !pane.item.followed)
                        onClicked: pane.report(AppController.setCurrentItemFollowed(true, volumesField.text))
                    }
                }
            }
        }

        Kirigami.InlineMessage {
            id: error

            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            showCloseButton: true
            visible: false
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                id: books

                model: AppController.books
                clip: true

                // The whole row toggles the selection (HIG: standard delegates); a shelf book
                // also has its Edit button, usable when the row is selected by its series.
                delegate: Item {
                    id: book

                    required property string id
                    required property string title
                    required property string subtitle
                    required property string selection

                    width: ListView.view.width
                    implicitHeight: check.implicitHeight

                    KD.CheckSubtitleDelegate {
                        id: check

                        anchors.fill: parent
                        rightPadding: editButton.visible ? editButton.width + Kirigami.Units.largeSpacing * 2 : Kirigami.Units.largeSpacing
                        text: book.title
                        subtitle: book.subtitle
                        icon.source: "image://komga/book/" + book.id + AppController.coverStamp
                        icon.width: Kirigami.Units.iconSizes.large
                        icon.height: Kirigami.Units.iconSizes.large
                        checked: book.selection !== "none"
                        enabled: pane.canSelect && book.selection !== "group"
                        onToggled: pane.report(AppController.setBookSelected(book.id, checked))
                    }

                    QQC2.ToolButton {
                        id: editButton

                        anchors.right: parent.right
                        anchors.rightMargin: Kirigami.Units.largeSpacing
                        anchors.verticalCenter: parent.verticalCenter
                        visible: book.id.startsWith("shelf:")
                        icon.name: "document-edit"
                        text: i18nc("@action:button", "Edit Book…")
                        display: QQC2.AbstractButton.IconOnly
                        onClicked: pane.report(editor.openBooks([book.id]))
                        QQC2.ToolTip.text: text
                        QQC2.ToolTip.visible: hovered
                        QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    }
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 4
                    visible: books.count === 0
                    text: AppController.booksLoading ? i18n("Loading books…") : i18n("No books")
                }
            }
        }
    }

    ShelfEditorDialog {
        id: editor

        onFindMetadata: lookup.start()
    }

    ShelfLookupDialog {
        id: lookup
    }
}
