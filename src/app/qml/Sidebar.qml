// SPDX-License-Identifier: MIT

import QtQuick
import QtQml.Models
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Leading pane: libraries, collections and read lists as collapsible sections
// (SPEC-APP.md §5).
QQC2.Pane {
    id: sidebar

    padding: 0
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    background: Rectangle {
        color: Kirigami.Theme.backgroundColor
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Kirigami.SearchField {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing
            onTextChanged: AppController.seriesSearch = text
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        QQC2.ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            QQC2.ScrollBar.horizontal.policy: QQC2.ScrollBar.AlwaysOff

            ListView {
                id: list

                model: AppController.sidebar
                clip: true

                delegate: DelegateChooser {
                    role: "kind"

                    // A section: clicking collapses or expands it.
                    DelegateChoice {
                        roleValue: "header"

                        QQC2.ItemDelegate {
                            id: header

                            required property string id
                            required property string title
                            required property string subtitle
                            required property bool expanded

                            width: ListView.view.width
                            topPadding: Kirigami.Units.largeSpacing
                            onClicked: AppController.toggleSection(id)
                            Accessible.name: expanded ? i18n("Collapse %1", title) : i18n("Expand %1", title)

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: header.expanded ? "go-down-symbolic" : "go-next-symbolic"
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                }

                                Kirigami.Heading {
                                    Layout.fillWidth: true
                                    level: 5
                                    text: header.title
                                    elide: Text.ElideRight
                                }

                                QQC2.Label {
                                    text: header.subtitle
                                    color: Kirigami.Theme.disabledTextColor
                                }
                            }
                        }
                    }

                    // A series, collection or read list; opens it in the content pane.
                    DelegateChoice {
                        QQC2.ItemDelegate {
                            id: row

                            required property string kind
                            required property string id
                            required property string title
                            required property string subtitle
                            required property string badge
                            required property bool current

                            width: ListView.view.width
                            highlighted: current
                            leftPadding: Kirigami.Units.largeSpacing + Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                            onClicked: AppController.openItem(kind, id)

                            QQC2.ToolTip.text: badge
                            QQC2.ToolTip.visible: hovered && badge !== ""
                            QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay

                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing

                                // Series cover before the name (collections and read lists have none).
                                Image {
                                    visible: row.kind === "series"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium * 1.5
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectFit
                                    sourceSize.height: Kirigami.Units.iconSizes.medium * 3
                                    source: row.kind === "series" ? "image://komga/series/" + row.id + AppController.coverStamp : ""
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: row.title
                                        font.bold: row.current
                                        elide: Text.ElideRight
                                    }

                                    QQC2.Label {
                                        Layout.fillWidth: true
                                        text: row.subtitle
                                        font: Kirigami.Theme.smallFont
                                        color: Kirigami.Theme.disabledTextColor
                                        elide: Text.ElideRight
                                    }
                                }

                                Kirigami.Icon {
                                    visible: row.badge !== ""
                                    source: "emblem-checked"
                                    implicitWidth: Kirigami.Units.iconSizes.small
                                    implicitHeight: Kirigami.Units.iconSizes.small
                                }
                            }
                        }
                    }
                }

                Kirigami.PlaceholderMessage {
                    anchors.centerIn: parent
                    width: parent.width - Kirigami.Units.gridUnit * 2
                    visible: list.count === 0 && AppController.komgaState === "ready"
                    text: AppController.libraryLoading ? i18n("Loading the library…") : i18n("No series")
                }
            }
        }
    }
}
