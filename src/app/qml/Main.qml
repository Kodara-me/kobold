// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kirigamiaddons.settings as KirigamiSettings
import me.kodara.kobold

// One main window: toolbar, sidebar, content and current Kobo pane (SPEC-APP.md §4).
Kirigami.ApplicationWindow {
    id: root

    title: i18n("Kobold")
    width: Kirigami.Units.gridUnit * 64
    height: Kirigami.Units.gridUnit * 40
    minimumWidth: Kirigami.Units.gridUnit * 30
    minimumHeight: Kirigami.Units.gridUnit * 20

    pageStack.globalToolBar.style: Kirigami.ApplicationHeaderStyle.ToolBar
    pageStack.initialPage: MainPage {
        onSettingsRequested: moduleId => configuration.open(moduleId)
        onAboutRequested: configuration.open("about")
    }

    KirigamiSettings.ConfigurationView {
        id: configuration
        objectName: "configuration"

        window: root
        modules: [
            KirigamiSettings.ConfigurationModule {
                moduleId: "komga"
                text: i18nc("@action:button", "Komga")
                icon.name: "network-server"
                page: () => Qt.createComponent("me.kodara.kobold", "KomgaSettingsPage")
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "shelves"
                text: i18nc("@action:button", "Shelves")
                icon.name: "folder-documents"
                page: () => Qt.createComponent("me.kodara.kobold", "ShelvesSettingsPage")
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "kobos"
                text: i18nc("@action:button", "Kobos")
                icon.name: "tablet"
                page: () => Qt.createComponent("me.kodara.kobold", "KobosSettingsPage")
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "diagnostics"
                text: i18nc("@action:button", "Diagnostics")
                icon.name: "tools-report-bug"
                page: () => Qt.createComponent("me.kodara.kobold", "DiagnosticsPage")
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "parental"
                text: i18nc("@action:button", "Parental Control")
                icon.name: "im-user-offline"
                page: () => Qt.createComponent("me.kodara.kobold", "ParentalSettingsPage")
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "appearance"
                text: i18nc("@action:button", "Appearance")
                icon.name: "preferences-desktop-theme-global"
                page: () => Qt.createComponent("me.kodara.kobold", "AppearanceSettingsPage")
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "cache"
                text: i18nc("@action:button", "Cover Cache")
                icon.name: "folder-pictures"
                page: () => Qt.createComponent("me.kodara.kobold", "CacheSettingsPage")
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "about"
                text: i18nc("@action:button", "About Kobold")
                icon.name: "help-about"
                page: () => Qt.createComponent("org.kde.kirigamiaddons.formcard", "AboutPage")
                category: i18nc("@title:group", "About")
            },
            KirigamiSettings.ConfigurationModule {
                moduleId: "aboutkde"
                text: i18nc("@action:button", "About KDE")
                icon.name: "kde"
                page: () => Qt.createComponent("org.kde.kirigamiaddons.formcard", "AboutKDE")
                category: i18nc("@title:group", "About")
            }
        ]
    }

    // An operation must not be cut by closing the window (SPEC-APP.md §8).
    property bool closeConfirmed: false

    onClosing: close => {
        if (AppController.busy && !root.closeConfirmed) {
            close.accepted = false;
            quitDialog.open();
        }
    }

    Kirigami.PromptDialog {
        id: quitDialog

        title: i18nc("@title:dialog", "Operation in Progress")
        subtitle: i18n("Kobold is still working with the Kobo. Quitting now may leave the sync unfinished.")
        standardButtons: Kirigami.Dialog.NoButton
        customFooterActions: [
            Kirigami.Action {
                text: i18nc("@action:button", "Quit Anyway")
                icon.name: "application-exit"
                onTriggered: {
                    root.closeConfirmed = true;
                    root.close();
                }
            },
            Kirigami.Action {
                text: i18nc("@action:button", "Keep Running")
                icon.name: "dialog-cancel"
                onTriggered: quitDialog.close()
            }
        ]
    }
}
