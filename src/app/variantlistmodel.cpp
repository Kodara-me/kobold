// SPDX-License-Identifier: MIT

#include "variantlistmodel.h"

namespace Kobold
{

VariantListModel::VariantListModel(const QByteArrayList &roles, QObject *parent)
    : QAbstractListModel(parent)
    , m_roles(roles)
{
}

int VariantListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant VariantListModel::data(const QModelIndex &index, int role) const
{
    const int column = role - Qt::UserRole;
    if (!index.isValid() || index.row() >= m_items.size() || column < 0 || column >= m_roles.size()) {
        return {};
    }
    return m_items.at(index.row()).value(QString::fromLatin1(m_roles.at(column)));
}

QHash<int, QByteArray> VariantListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    for (int i = 0; i < m_roles.size(); ++i) {
        names.insert(Qt::UserRole + i, m_roles.at(i));
    }
    return names;
}

void VariantListModel::setItems(const QList<QVariantMap> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    Q_EMIT countChanged();
}

void VariantListModel::append(const QVariantMap &item)
{
    beginInsertRows(QModelIndex(), int(m_items.size()), int(m_items.size()));
    m_items.append(item);
    endInsertRows();
    Q_EMIT countChanged();
}

void VariantListModel::clear()
{
    setItems({});
}

const QList<QVariantMap> &VariantListModel::items() const
{
    return m_items;
}

QVariantMap VariantListModel::get(int row) const
{
    return row >= 0 && row < m_items.size() ? m_items.at(row) : QVariantMap();
}

} // namespace Kobold
