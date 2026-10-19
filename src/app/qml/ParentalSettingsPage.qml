// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.formcard as FormCard
import me.kodara.kobold

// Settings: parental control (SPEC-PARENTAL.md §2–§4).
FormCard.FormCardPage {
    id: page

    title: i18nc("@title", "Parental Control")

    Component.onCompleted: AppController.loadKomgaGenres()

    ParentalPinDialog {
        id: pinDialog
        objectName: "parentalPinDialog"
        // Settings pages live in their own window: open there, not in the main window.
        parent: page.QQC2.Overlay.overlay
    }

    FormCard.FormCard {
        Layout.topMargin: Kirigami.Units.largeSpacing

        FormCard.FormTextDelegate {
            text: !AppController.parentalEnabled ? i18n("Parental control is off")
                : AppController.parentalLocked ? i18n("Adult content is hidden")
                : i18n("Adult content is shown until Kobold is locked again")
            description: i18n("Adult series are hidden in Kobold until the PIN is entered. Nothing changes on the Kobos: hidden books stay in the selections and keep syncing. This is not a security barrier; to really restrict content, give the person a Komga account with an age restriction.")
            textItem.wrapMode: Text.WordWrap
            descriptionItem.wrapMode: Text.WordWrap
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormButtonDelegate {
            visible: !AppController.parentalEnabled
            text: i18nc("@action:button", "Turn On…")
            icon.name: "object-locked"
            onClicked: pinDialog.ask("enable")
        }

        FormCard.FormButtonDelegate {
            visible: AppController.parentalEnabled && AppController.parentalLocked
            text: i18nc("@action:button", "Unlock…")
            description: i18n("Needed to change these settings.")
            icon.name: "object-unlocked"
            onClicked: pinDialog.ask("unlock")
        }

        FormCard.FormButtonDelegate {
            visible: AppController.parentalEnabled && !AppController.parentalLocked
            text: i18nc("@action:button", "Lock Now")
            icon.name: "object-locked"
            onClicked: AppController.lockParental()
        }

        FormCard.FormButtonDelegate {
            visible: AppController.parentalEnabled
            text: i18nc("@action:button", "Turn Off…")
            icon.name: "edit-delete-remove"
            onClicked: pinDialog.ask("disable")
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "Adult Content")
        visible: AppController.parentalEnabled
    }

    FormCard.FormCard {
        visible: AppController.parentalEnabled
        enabled: !AppController.parentalLocked

        FormCard.FormSpinBoxDelegate {
            id: ageLimit

            label: i18nc("@label:spinbox", "Age rating from:")
            from: 0
            to: 21
            value: AppController.parentalAgeLimit
            textFromValue: (value, locale) => value === 0 ? i18nc("@item:inrange age rule off", "Off") : Number(value).toLocaleString(locale, 'f', 0)
            onValueModified: rulesMessage.text = AppController.setParentalRules(value, AppController.parentalKeywords)
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormTextFieldDelegate {
            id: keywords

            label: i18nc("@label:textbox", "Genres containing (separated by commas):")
            text: AppController.parentalKeywords.join(", ")
            onEditingFinished: rulesMessage.text = AppController.setParentalRules(ageLimit.value, text.split(",").map(word => word.trim()).filter(word => word !== ""))
        }

        FormCard.FormTextDelegate {
            id: rulesMessage

            visible: text !== ""
            textItem.color: Kirigami.Theme.negativeTextColor
        }

        FormCard.FormDelegateSeparator {}

        FormCard.FormSpinBoxDelegate {
            label: i18nc("@label:spinbox", "Lock again after (minutes):")
            from: 0
            to: 240
            value: AppController.parentalAutoLockMinutes
            textFromValue: (value, locale) => value === 0 ? i18nc("@item:inrange lock only on quit", "Only when quitting") : Number(value).toLocaleString(locale, 'f', 0)
            onValueModified: AppController.setParentalAutoLockMinutes(value)
        }
    }

    FormCard.FormHeader {
        title: i18nc("@title:group", "Komga Genres")
        visible: AppController.parentalEnabled && genres.count > 0
    }

    FormCard.FormCard {
        visible: AppController.parentalEnabled && genres.count > 0

        Repeater {
            id: genres

            model: AppController.komgaGenres

            delegate: FormCard.FormTextDelegate {
                required property var modelData

                text: modelData.name
                description: modelData.adult ? i18n("Adult: hidden while locked") : ""
                icon.name: modelData.adult ? "object-locked" : ""
            }
        }
    }
}
