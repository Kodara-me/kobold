// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Every book a Kobo knows, to hide, show or mark them, with the history of changes and the
// backups of its database (SPEC-KOBO-BOOKS.md §6).
Kirigami.ApplicationWindow {
    id: window

    readonly property var books: AppController.koboBooks

    title: i18nc("@title:window", "Books on %1", books.koboName)
    width: Kirigami.Units.gridUnit * 46
    height: Kirigami.Units.gridUnit * 36
    minimumWidth: Kirigami.Units.gridUnit * 24
    minimumHeight: Kirigami.Units.gridUnit * 20

    pageStack.initialPage: Kirigami.Page {
        id: page

        title: window.title
        padding: 0
        globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None

        header: QQC2.ToolBar {
            contentItem: RowLayout {
                QQC2.TabBar {
                    id: tabs

                    Layout.fillWidth: true

                    QQC2.TabButton {
                        text: i18nc("@title:tab", "Books")
                    }
                    QQC2.TabButton {
                        text: i18nc("@title:tab", "History")
                    }
                    QQC2.TabButton {
                        text: i18nc("@title:tab", "Backups")
                    }
                }

                QQC2.BusyIndicator {
                    running: window.books.busy
                    visible: running
                    implicitHeight: Kirigami.Units.iconSizes.smallMedium
                    implicitWidth: implicitHeight
                }

                QQC2.ToolButton {
                    icon.name: "view-refresh"
                    text: i18nc("@action:button", "Refresh")
                    display: QQC2.AbstractButton.IconOnly
                    enabled: !window.books.busy
                    onClicked: window.books.reload()
                    QQC2.ToolTip.text: text
                    QQC2.ToolTip.visible: hovered
                }
            }
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.margins: Kirigami.Units.smallSpacing
                visible: text !== ""
                type: window.books.errorString !== "" ? Kirigami.MessageType.Error : Kirigami.MessageType.Positive
                text: window.books.errorString !== "" ? window.books.errorString : window.books.message
                showCloseButton: true
                actions: [
                    Kirigami.Action {
                        visible: window.books.errorString === "" && window.books.message !== ""
                        text: i18nc("@action:button", "Eject Now")
                        icon.name: "media-eject"
                        onTriggered: {
                            AppController.ejectKobo(window.books.serial);
                            window.close();
                        }
                    }
                ]
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: tabs.currentIndex

                // Books.
                ColumnLayout {
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.smallSpacing

                        Kirigami.SearchField {
                            Layout.fillWidth: true
                            onTextChanged: window.books.filterText = text
                        }

                        QQC2.ComboBox {
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { text: i18nc("@item:inlistbox origin filter", "All origins"), value: "" },
                                { text: i18nc("@info book origin", "Kobo Store"), value: "store" },
                                { text: i18nc("@info book origin", "Komga"), value: "komga" },
                                { text: i18nc("@info book origin", "Kobold"), value: "kobold" },
                                { text: i18nc("@info book origin", "Other file"), value: "other" }
                            ]
                            onActivated: window.books.filterOrigin = currentValue
                        }

                        QQC2.ComboBox {
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { text: i18nc("@item:inlistbox visibility filter", "Shown and hidden"), value: "" },
                                { text: i18nc("@item:inlistbox visibility filter", "Shown"), value: "shown" },
                                { text: i18nc("@item:inlistbox visibility filter", "Hidden"), value: "hidden" }
                            ]
                            onActivated: window.books.filterVisibility = currentValue
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    QQC2.ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ListView {
                            id: bookList

                            model: window.books.books
                            clip: true

                            delegate: QQC2.ItemDelegate {
                                id: row

                                required property string contentId
                                required property string title
                                required property string details
                                required property string originLabel
                                required property bool hidden
                                required property bool marked
                                required property string thumbnail

                                width: ListView.view.width
                                onClicked: window.books.setChecked(row.contentId, !row.marked)

                                contentItem: RowLayout {
                                    spacing: Kirigami.Units.largeSpacing

                                    QQC2.CheckBox {
                                        checked: row.marked
                                        onToggled: window.books.setChecked(row.contentId, checked)
                                    }

                                    // The Kobo's own thumbnail (SPEC-KOBO-BOOKS.md §2).
                                    Item {
                                        implicitWidth: Kirigami.Units.gridUnit * 2
                                        implicitHeight: Kirigami.Units.gridUnit * 3

                                        Image {
                                            id: cover

                                            anchors.fill: parent
                                            fillMode: Image.PreserveAspectFit
                                            asynchronous: true
                                            sourceSize.height: Kirigami.Units.gridUnit * 3
                                            source: row.thumbnail
                                            opacity: row.hidden ? 0.4 : 1
                                        }

                                        Kirigami.Icon {
                                            anchors.centerIn: parent
                                            implicitWidth: Kirigami.Units.iconSizes.medium
                                            implicitHeight: implicitWidth
                                            visible: cover.status !== Image.Ready
                                            source: "application-epub+zip"
                                            opacity: 0.5
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 0

                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            text: row.title
                                            elide: Text.ElideRight
                                            color: row.hidden ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
                                        }

                                        QQC2.Label {
                                            Layout.fillWidth: true
                                            text: row.details
                                            elide: Text.ElideRight
                                            font: Kirigami.Theme.smallFont
                                            color: Kirigami.Theme.disabledTextColor
                                        }
                                    }

                                    QQC2.Label {
                                        text: row.originLabel
                                        font: Kirigami.Theme.smallFont
                                        color: Kirigami.Theme.disabledTextColor
                                    }

                                    Kirigami.Icon {
                                        source: row.hidden ? "view-hidden" : "view-visible"
                                        implicitWidth: Kirigami.Units.iconSizes.small
                                        implicitHeight: implicitWidth
                                        opacity: row.hidden ? 1 : 0.4
                                        QQC2.ToolTip.text: row.hidden ? i18nc("@info:tooltip", "Hidden on the Kobo") : i18nc("@info:tooltip", "Shown on the Kobo")
                                        QQC2.ToolTip.visible: hoverHandler.hovered

                                        HoverHandler {
                                            id: hoverHandler
                                        }
                                    }
                                }
                            }

                            Kirigami.PlaceholderMessage {
                                anchors.centerIn: parent
                                width: parent.width - Kirigami.Units.gridUnit * 4
                                visible: bookList.count === 0 && !window.books.busy
                                text: window.books.totalCount === 0 ? i18n("No books") : i18n("No book matches the filters")
                            }
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.smallSpacing

                        QQC2.Label {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            color: Kirigami.Theme.disabledTextColor
                            text: {
                                let parts = [i18np("1 book", "%1 books", window.books.totalCount),
                                             i18np("1 hidden", "%1 hidden", window.books.hiddenCount),
                                             i18np("1 checked", "%1 checked", window.books.checkedCount)];
                                if (window.books.adultCount > 0) {
                                    parts.push(i18np("1 left out by parental control", "%1 left out by parental control", window.books.adultCount));
                                }
                                return parts.join(" · ");
                            }
                        }

                        QQC2.Button {
                            text: i18nc("@action:button", "Check All Shown")
                            icon.name: "edit-select-all"
                            enabled: bookList.count > 0
                            onClicked: window.books.checkShown(true)
                        }

                        QQC2.Button {
                            text: i18nc("@action:button", "Uncheck")
                            icon.name: "edit-select-none"
                            enabled: window.books.checkedCount > 0
                            onClicked: window.books.checkShown(false)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.smallSpacing
                        Layout.rightMargin: Kirigami.Units.smallSpacing
                        Layout.bottomMargin: Kirigami.Units.smallSpacing

                        Item {
                            Layout.fillWidth: true
                        }

                        Repeater {
                            model: [
                                { action: "hide", text: i18nc("@action:button", "Hide"), icon: "view-hidden" },
                                { action: "show", text: i18nc("@action:button", "Show"), icon: "view-visible" },
                                { action: "markRead", text: i18nc("@action:button", "Mark as Read"), icon: "checkmark" },
                                { action: "markUnread", text: i18nc("@action:button", "Mark as Unread"), icon: "edit-undo" },
                                { action: "delete", text: i18nc("@action:button", "Delete…"), icon: "edit-delete" }
                            ]

                            delegate: QQC2.Button {
                                required property var modelData

                                text: modelData.text
                                icon.name: modelData.icon
                                enabled: window.books.checkedCount > 0 && !window.books.busy && !AppController.busy
                                         && window.books.preview(modelData.action) !== ""
                                onClicked: confirm.ask(modelData.action, modelData.text)
                            }
                        }
                    }
                }

                // History.
                QQC2.ScrollView {
                    ListView {
                        id: historyList

                        model: window.books.history
                        clip: true

                        delegate: QQC2.ItemDelegate {
                            id: change

                            required property string batch
                            required property string date
                            required property string lines
                            required property bool undone
                            required property bool isUndo

                            width: ListView.view.width
                            down: false

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.largeSpacing

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        font.bold: true
                                        text: change.isUndo ? i18nc("@info date, undo", "%1 · undo", change.date)
                                                            : change.undone ? i18nc("@info date, undone", "%1 · undone", change.date) : change.date
                                    }

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        wrapMode: Text.Wrap
                                        text: change.lines
                                        color: change.undone ? Kirigami.Theme.disabledTextColor : Kirigami.Theme.textColor
                                    }
                                }

                                QQC2.Button {
                                    visible: !change.isUndo
                                    text: i18nc("@action:button", "Undo")
                                    icon.name: "edit-undo"
                                    enabled: !change.undone && !window.books.busy && !AppController.busy
                                    onClicked: window.books.undo(change.batch)
                                }
                            }
                        }

                        Kirigami.PlaceholderMessage {
                            anchors.centerIn: parent
                            width: parent.width - Kirigami.Units.gridUnit * 4
                            visible: historyList.count === 0
                            text: i18n("No change yet")
                            explanation: i18n("Changes made here are listed with a way to undo them.")
                        }
                    }
                }

                // Backups.
                QQC2.ScrollView {
                    ListView {
                        id: backupList

                        model: window.books.backups
                        clip: true

                        delegate: QQC2.ItemDelegate {
                            id: backup

                            required property string path
                            required property string date

                            width: ListView.view.width
                            down: false

                            contentItem: RowLayout {
                                QQC2.Label {
                                    Layout.fillWidth: true
                                    text: backup.date
                                    elide: Text.ElideRight
                                }

                                QQC2.Button {
                                    text: i18nc("@action:button", "Restore…")
                                    icon.name: "document-revert"
                                    enabled: !window.books.busy && !AppController.busy
                                    onClicked: restoreDialog.ask(backup.path, backup.date)
                                }
                            }
                        }

                        Kirigami.PlaceholderMessage {
                            anchors.centerIn: parent
                            width: parent.width - Kirigami.Units.gridUnit * 4
                            visible: backupList.count === 0
                            text: i18n("No backup yet")
                            explanation: i18n("The Kobo's database is backed up before every change.")
                        }
                    }
                }
            }
        }
    }

    Kirigami.PromptDialog {
        id: confirm

        property string action

        function ask(action, label) {
            confirm.action = action;
            confirm.title = label.replace("…", "");
            let text = window.books.preview(action) + "\n" + i18n("The Kobo's database is backed up first.");
            if (action === "delete") {
                text += "\n\n" + i18n("Kobo Store books are left out: the store would bring them back; hide them instead. Files are kept with the backup, so that Undo can bring them back. A book still selected in Kobold, or synced by Komga, comes back at the next sync.");
            }
            confirm.subtitle = text;
            confirm.open();
        }

        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: confirm.action === "delete" ? i18nc("@action:button", "Delete") : i18nc("@action:button", "Apply")
                icon.name: confirm.action === "delete" ? "edit-delete" : "dialog-ok-apply"
                onTriggered: {
                    window.books.apply(confirm.action);
                    confirm.close();
                }
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Cancel")
                icon.name: "dialog-cancel"
                onTriggered: confirm.close()
            }
        ]
    }

    Kirigami.PromptDialog {
        id: restoreDialog

        property string path

        function ask(path, date) {
            restoreDialog.path = path;
            restoreDialog.subtitle = i18n("The Kobo's database will be replaced by the backup of %1. The current one is backed up first.", date);
            restoreDialog.open();
        }

        title: i18nc("@title:dialog", "Restore Backup")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Restore")
                icon.name: "document-revert"
                onTriggered: {
                    window.books.restore(restoreDialog.path);
                    restoreDialog.close();
                }
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Cancel")
                icon.name: "dialog-cancel"
                onTriggered: restoreDialog.close()
            }
        ]
    }
}
