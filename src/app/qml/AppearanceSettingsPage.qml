// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import me.kodara.kobold

// Settings: the application icon (SPEC-APP.md §12.1).
FormCard.FormCardPage {
    id: page

    title: i18nc("@title", "Appearance")

    FormCard.FormHeader {
        title: i18nc("@title:group", "Application Icon")
    }

    FormCard.FormCard {
        FormCard.AbstractFormDelegate {
            background: null
            contentItem: RowLayout {
                spacing: Kirigami.Units.largeSpacing

                Repeater {
                    model: [
                        { id: "peek", label: i18nc("@item:inlistbox application icon", "Peeking Kobold") },
                        { id: "sync", label: i18nc("@item:inlistbox application icon", "Sync") },
                        { id: "face", label: i18nc("@item:inlistbox application icon", "Kobold") }
                    ]

                    delegate: QQC2.ItemDelegate {
                        id: choice

                        required property var modelData

                        Layout.fillWidth: true
                        checkable: true
                        checked: AppController.appIcon === modelData.id
                        highlighted: checked
                        onClicked: AppController.appIcon = modelData.id
                        Accessible.name: modelData.label

                        contentItem: ColumnLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Image {
                                Layout.alignment: Qt.AlignHCenter
                                Layout.preferredWidth: Kirigami.Units.iconSizes.huge
                                Layout.preferredHeight: Kirigami.Units.iconSizes.huge
                                sourceSize.width: Kirigami.Units.iconSizes.huge * 2
                                sourceSize.height: Kirigami.Units.iconSizes.huge * 2
                                source: AppController.appIconUrl(choice.modelData.id)
                            }

                            QQC2.RadioButton {
                                Layout.alignment: Qt.AlignHCenter
                                text: choice.modelData.label
                                checked: choice.checked
                                onClicked: AppController.appIcon = choice.modelData.id
                            }
                        }
                    }
                }
            }
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormTextDelegate {
            text: i18n("The window and the About page change at once. The launcher and the task manager may need a moment, or a new session.")
            textItem.wrapMode: Text.WordWrap
        }
    }
}
