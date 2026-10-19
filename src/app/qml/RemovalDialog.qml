// SPDX-License-Identifier: MIT

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import me.kodara.kobold

// Asks, book by book, before the sync removes anything from the Kobo (SPEC-UNSYNC.md §3).
// One dialog per case: books that left the selection, then books their library no longer has.
Kirigami.Dialog {
    id: dialog

    property var questions: []
    property int index: 0
    property var answers: ({})
    // Answer given with "Apply to All" for the rest of the current case, or undefined.
    property var answerForCase

    readonly property var current: index < questions.length ? questions[index] : null
    readonly property bool goneFromKomga: current !== null && current.kind === 3
    readonly property int caseCount: {
        let count = 0;
        for (const question of questions) {
            if (current !== null && question.kind === current.kind) {
                ++count;
            }
        }
        return count;
    }

    function start() {
        dialog.questions = AppController.removalQuestions;
        dialog.index = 0;
        dialog.answers = {};
        dialog.answerForCase = undefined;
        applyToAll.checked = false;
        dialog.open();
    }

    function answer(remove) {
        const kind = current.kind;
        answers[current.bookId] = remove;
        if (applyToAll.checked) {
            dialog.answerForCase = remove;
        }
        ++dialog.index;
        // Answer the rest of the case at once, then move to the next case.
        while (dialog.index < questions.length && dialog.answerForCase !== undefined && questions[dialog.index].kind === kind) {
            answers[questions[dialog.index].bookId] = dialog.answerForCase;
            ++dialog.index;
        }
        if (dialog.index < questions.length && questions[dialog.index].kind !== kind) {
            dialog.answerForCase = undefined;
            applyToAll.checked = false;
        }
        if (dialog.index >= questions.length) {
            AppController.answerRemovals(dialog.answers);
            dialog.questions = [];
            dialog.close();
        }
    }

    title: goneFromKomga ? i18nc("@title:dialog", "Books No Longer in Their Library") : i18nc("@title:dialog", "Remove Books from the Kobo?")
    padding: Kirigami.Units.largeSpacing
    preferredWidth: Kirigami.Units.gridUnit * 26
    closePolicy: QQC2.Popup.NoAutoClose
    showCloseButton: false
    standardButtons: Kirigami.Dialog.NoButton

    Connections {
        target: AppController

        function onRemovalsToConfirm() {
            dialog.start();
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.mediumSpacing

        QQC2.Label {
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 26
            wrapMode: Text.WordWrap
            text: dialog.goneFromKomga ? i18n("Their library (Komga or a shelf) no longer has these books. The copy on the Kobo is the only one left.")
                                       : i18n("These books left the selection. Komga still has them.")
        }

        Kirigami.Heading {
            Layout.fillWidth: true
            level: 4
            wrapMode: Text.WordWrap
            text: dialog.current ? dialog.current.label : ""
        }

        QQC2.Label {
            Layout.fillWidth: true
            text: dialog.current ? i18nc("@info book size · position", "%1 · %2 of %3", dialog.current.size, dialog.index + 1, dialog.questions.length) : ""
            color: Kirigami.Theme.disabledTextColor
        }

        QQC2.CheckBox {
            id: applyToAll

            visible: dialog.caseCount > 1
            text: dialog.goneFromKomga ? i18nc("@option:check", "Apply to all books no longer in their library")
                                       : i18nc("@option:check", "Apply to all books that left the selection")
        }
    }

    customFooterActions: [
        Kirigami.Action {
            text: i18nc("@action:button", "Remove")
            icon.name: "edit-delete"
            onTriggered: dialog.answer(true)
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Keep on the Kobo")
            icon.name: "document-save"
            onTriggered: dialog.answer(false)
        },
        Kirigami.Action {
            text: i18nc("@action:button", "Cancel Sync")
            icon.name: "dialog-cancel"
            onTriggered: {
                dialog.questions = [];
                dialog.close();
                AppController.cancelRemovals();
            }
        }
    ]
}
