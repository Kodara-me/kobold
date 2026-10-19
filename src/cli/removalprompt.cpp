// SPDX-License-Identifier: MIT

#include "removalprompt.h"

#include "console.h"

#include <KLocalizedString>

#include <QLocale>

namespace Kobold::Cli
{

QHash<QString, bool> askRemovals(const QList<RemovalQuestion> &questions, const HiddenContent &hidden, bool *cancelled)
{
    *cancelled = false;
    // One letter per answer, translated with the prompt.
    const QString removeKey = i18nc("@info answer letter for remove", "r");
    const QString keepKey = i18nc("@info answer letter for keep", "k");
    const QString removeAllKey = i18nc("@info answer letter for remove all", "a");
    const QString keepAllKey = i18nc("@info answer letter for keep all", "l");
    const QString cancelKey = i18nc("@info answer letter for cancel", "c");

    QHash<QString, bool> answers;
    std::optional<RemovalQuestion::Case> currentCase;
    std::optional<bool> answerForAll;
    for (const RemovalQuestion &question : questions) {
        if (currentCase != question.kind) {
            currentCase = question.kind;
            answerForAll.reset();
            switch (question.kind) {
            case RemovalQuestion::Case::Deselected:
                printLine(i18n("These books left the selection; Komga still has them:"));
                break;
            case RemovalQuestion::Case::LocalDeselected:
                printLine(i18n("These local files left the selection; the computer still has them:"));
                break;
            case RemovalQuestion::Case::GoneFromKomga:
                printLine(i18n("Their library (Komga or a shelf) no longer has these books; the Kobo has the only copy:"));
                break;
            case RemovalQuestion::Case::LocalMissing:
                printLine(i18n("These local files are gone from the computer; the Kobo has the only copy:"));
                break;
            }
        }
        if (answerForAll) {
            answers.insert(question.bookId, *answerForAll);
            continue;
        }
        const QString name = hidden.book(question.bookId, question.seriesId, question.label);
        const QString size = QLocale().formattedDataSize(question.sizeBytes);
        const QString prompt = question.removeByDefault()
            ? i18n("  %1 (%2) — [R]emove, [k]eep, remove [a]ll, keep a[l]l, [c]ancel? ", name, size)
            : i18n("  %1 (%2) — [r]emove, [K]eep, remove [a]ll, keep a[l]l, [c]ancel? ", name, size);
        for (;;) {
            const QString answer = readLine(prompt).toLower();
            if (answer.isEmpty()) {
                answers.insert(question.bookId, question.removeByDefault());
            } else if (answer.startsWith(removeKey)) {
                answers.insert(question.bookId, true);
            } else if (answer.startsWith(keepKey)) {
                answers.insert(question.bookId, false);
            } else if (answer.startsWith(removeAllKey)) {
                answerForAll = true;
                answers.insert(question.bookId, true);
            } else if (answer.startsWith(keepAllKey)) {
                answerForAll = false;
                answers.insert(question.bookId, false);
            } else if (answer.startsWith(cancelKey)) {
                *cancelled = true;
                return {};
            } else {
                continue;
            }
            break;
        }
    }
    return answers;
}

} // namespace Kobold::Cli
