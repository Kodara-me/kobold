// SPDX-License-Identifier: MIT

#pragma once

#include "kobobooks.h"
#include "variantlistmodel.h"

#include <QObject>
#include <QSet>
#include <qqmlregistration.h>

#include <functional>

namespace Kobold
{

// The Manage Books window of one Kobo (SPEC-KOBO-BOOKS.md §6): its books with filters and
// check boxes, the changes, their history and the backups. Reads and writes run on the
// thread pool: the database sits on a slow USB device.
class KoboBooksController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application controller")

    Q_PROPERTY(QString serial READ serial NOTIFY koboChanged)
    Q_PROPERTY(QString koboName READ koboName NOTIFY koboChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // Error of the last load or change; empty when it went well.
    Q_PROPERTY(QString errorString READ errorString NOTIFY resultChanged)
    // Outcome of the last change, e.g. "3 books changed."
    Q_PROPERTY(QString message READ message NOTIFY resultChanged)
    Q_PROPERTY(Kobold::VariantListModel *books READ books CONSTANT)
    Q_PROPERTY(Kobold::VariantListModel *history READ history CONSTANT)
    Q_PROPERTY(Kobold::VariantListModel *backups READ backups CONSTANT)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY booksChanged)
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY booksChanged)
    // Books left out by parental control.
    Q_PROPERTY(int adultCount READ adultCount NOTIFY booksChanged)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY booksChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filtersChanged)
    // "", "store", "komga", "kobold" or "other".
    Q_PROPERTY(QString filterOrigin READ filterOrigin WRITE setFilterOrigin NOTIFY filtersChanged)
    // "", "shown" or "hidden".
    Q_PROPERTY(QString filterVisibility READ filterVisibility WRITE setFilterVisibility NOTIFY filtersChanged)

public:
    struct Context {
        // Mount point of a connected Kobo, empty otherwise.
        std::function<QString(const QString &serial)> mountPoint;
        std::function<QString(const QString &serial)> koboName;
        std::function<KoboBooksAdultFilter()> adultFilter;
        // True while a sync or another operation uses the Kobos.
        std::function<bool()> operationRunning;
        // Backups folder; the default one when empty (tests set it).
        QString backupsRoot;
    };

    explicit KoboBooksController(const Context &context, QObject *parent = nullptr);

    QString serial() const
    {
        return m_serial;
    }
    QString koboName() const;
    bool busy() const
    {
        return m_busy;
    }
    QString errorString() const
    {
        return m_errorString;
    }
    QString message() const
    {
        return m_message;
    }
    VariantListModel *books() const
    {
        return m_booksModel;
    }
    VariantListModel *history() const
    {
        return m_historyModel;
    }
    VariantListModel *backups() const
    {
        return m_backupsModel;
    }
    int totalCount() const
    {
        return int(m_all.size());
    }
    int hiddenCount() const;
    int adultCount() const
    {
        return m_adultCount;
    }
    int checkedCount() const
    {
        return int(m_checked.size());
    }
    QString filterText() const
    {
        return m_filterText;
    }
    void setFilterText(const QString &text);
    QString filterOrigin() const
    {
        return m_filterOrigin;
    }
    void setFilterOrigin(const QString &origin);
    QString filterVisibility() const
    {
        return m_filterVisibility;
    }
    void setFilterVisibility(const QString &visibility);

    // Loads the books, history and backups of a connected Kobo.
    Q_INVOKABLE void open(const QString &serial);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void setChecked(const QString &contentId, bool checked);
    // Checks or unchecks every book the filters show.
    Q_INVOKABLE void checkShown(bool checked);
    // What apply(action) would change, e.g. "3 books will be hidden."; empty when nothing.
    // action: "hide", "show", "markRead" or "markUnread".
    Q_INVOKABLE QString preview(const QString &action) const;
    Q_INVOKABLE void apply(const QString &action);
    // Reverts a batch of the history.
    Q_INVOKABLE void undo(const QString &batch);
    Q_INVOKABLE void restore(const QString &backupPath);

Q_SIGNALS:
    void koboChanged();
    void busyChanged();
    void resultChanged();
    void booksChanged();
    void filtersChanged();

private:
    QList<std::pair<QString, KoboBooks::Change>> changesFor(const QString &action) const;
    bool canWrite();
    void setBusy(bool busy);
    void rebuildBooks();
    void rebuildHistory();
    void rebuildBackups();
    // Runs a change on the thread pool, then reloads.
    void runChange(std::function<KoboBooks::ApplyResult(KoboBooks &)> change, const QString &doneMessage = QString());

    Context m_context;
    QString m_serial;
    QString m_mountPoint;
    bool m_busy = false;
    QString m_errorString;
    QString m_message;
    QList<KoboBook> m_all;
    QList<KoboBookHistoryEntry> m_historyEntries;
    QList<KoboBackup> m_backupList;
    int m_adultCount = 0;
    QSet<QString> m_checked;
    QString m_filterText;
    QString m_filterOrigin;
    QString m_filterVisibility;
    VariantListModel *m_booksModel;
    VariantListModel *m_historyModel;
    VariantListModel *m_backupsModel;
};

} // namespace Kobold
