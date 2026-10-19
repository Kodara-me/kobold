// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Finds metadata online for a shelf book or series (SPEC-SHELF.md §4.3): the results with their
// cover, then each field side by side; only the checked fields are applied.
Kirigami.Dialog {
    id: dialog

    readonly property var editor: AppController.shelfEditor
    // "results" or "compare".
    property string step: "results"

    function start() {
        query.text = editor.lookupText;
        dialog.step = "results";
        editor.loadComicVineKey();
        dialog.open();
        editor.search(query.text);
    }

    title: i18nc("@title:dialog", "Find Metadata")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 34
    standardButtons: Kirigami.Dialog.NoButton

    Connections {
        target: dialog.editor

        function onLookupFinished(ok) {
            if (ok && dialog.step === "compare") {
                dialog.close();
            }
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 34
            wrapMode: Text.Wrap
            color: Kirigami.Theme.disabledTextColor
            text: i18n("Searches Open Library and Google Books, and ComicVine for comics when its key is set. Nothing is changed until you apply fields.")
        }

        RowLayout {
            Layout.fillWidth: true
            visible: dialog.step === "results"

            Kirigami.SearchField {
                id: query

                Layout.fillWidth: true
                onAccepted: dialog.editor.search(text)
            }

            QQC2.Button {
                text: i18nc("@action:button", "Search")
                icon.name: "edit-find"
                enabled: !dialog.editor.busy && query.text.trim() !== ""
                onClicked: dialog.editor.search(query.text)
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Error
            visible: text !== ""
            text: dialog.editor.errorString
        }

        QQC2.BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: dialog.editor.busy
            running: visible
        }

        // Results.
        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 22
            visible: dialog.step === "results" && !dialog.editor.busy

            ListView {
                model: dialog.editor.results
                clip: true

                delegate: QQC2.ItemDelegate {
                    id: result

                    required property int index
                    required property string label
                    required property string coverUrl

                    width: ListView.view.width
                    onClicked: {
                        dialog.editor.choose(result.index);
                        dialog.step = "compare";
                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing

                        Image {
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 2.5
                            Layout.preferredHeight: Kirigami.Units.gridUnit * 3.5
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            source: result.coverUrl
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            text: result.label
                        }
                    }
                }
            }
        }

        // One result, field by field.
        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.gridUnit * 22
            visible: dialog.step === "compare" && !dialog.editor.busy

            ListView {
                model: dialog.editor.comparison
                clip: true

                delegate: QQC2.CheckDelegate {
                    id: compared

                    required property string field
                    required property string label
                    required property string current
                    required property string found
                    required checked

                    width: ListView.view.width
                    onToggled: dialog.editor.setCompared(compared.field, checked)

                    contentItem: ColumnLayout {
                        spacing: 0

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.rightMargin: compared.indicator.width + Kirigami.Units.largeSpacing
                            font.bold: true
                            text: compared.label
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.rightMargin: compared.indicator.width + Kirigami.Units.largeSpacing
                            elide: Text.ElideRight
                            color: Kirigami.Theme.disabledTextColor
                            text: i18nc("@info current value", "Now: %1", compared.current === "" ? "—" : compared.current)
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.rightMargin: compared.indicator.width + Kirigami.Units.largeSpacing
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            text: i18nc("@info found value", "Found: %1", compared.found)
                        }
                    }
                }
            }
        }

        QQC2.CheckBox {
            id: copyToVolumes

            visible: dialog.step === "compare" && dialog.editor.kind === "series"
            text: i18nc("@option:check", "Also give the genres and age to every volume")
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Back to Results")
            icon.name: "go-previous"
            visible: dialog.step === "compare"
            onTriggered: dialog.step = "results"
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Apply Checked Fields")
            icon.name: "dialog-ok-apply"
            visible: dialog.step === "compare"
            enabled: !dialog.editor.busy
            onTriggered: dialog.editor.applyLookup(copyToVolumes.checked)
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Close")
            icon.name: "dialog-close"
            onTriggered: dialog.close()
        }
    ]
}
