// SPDX-License-Identifier: MIT

#pragma once

#include <QAbstractListModel>
#include <QByteArrayList>
#include <QVariantMap>
#include <qqmlregistration.h>

namespace Kobold
{

// List model whose rows are QVariantMaps; each key listed in the constructor is a role
// of the same name in QML.
class VariantListModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application controller")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit VariantListModel(const QByteArrayList &roles = {}, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QList<QVariantMap> &items);
    void append(const QVariantMap &item);
    void clear();
    const QList<QVariantMap> &items() const;

    Q_INVOKABLE QVariantMap get(int row) const;

Q_SIGNALS:
    void countChanged();

private:
    QByteArrayList m_roles;
    QList<QVariantMap> m_items;
};

} // namespace Kobold
