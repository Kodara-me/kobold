// SPDX-License-Identifier: MIT

#include "volumerange.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace Kobold
{

namespace
{
QString formatNumber(double number)
{
    return QString::number(number, 'g', 12);
}
}

std::optional<VolumeRange> VolumeRange::parse(const QString &text)
{
    static const QRegularExpression itemPattern(QStringLiteral("^(\\d+(?:\\.\\d+)?)(?:(-)(\\d+(?:\\.\\d+)?)?)?$"));

    QString compact = text;
    compact.remove(QRegularExpression(QStringLiteral("\\s")));
    if (compact.isEmpty()) {
        return std::nullopt;
    }

    VolumeRange range;
    const QStringList parts = compact.split(QLatin1Char(','));
    for (const QString &part : parts) {
        const QRegularExpressionMatch match = itemPattern.match(part);
        if (!match.hasMatch()) {
            return std::nullopt;
        }
        Item item;
        item.from = match.captured(1).toDouble();
        if (match.captured(2).isEmpty()) {
            item.to = item.from;
        } else if (!match.captured(3).isEmpty()) {
            item.to = match.captured(3).toDouble();
            if (*item.to < item.from) {
                return std::nullopt;
            }
        }
        range.m_items.append(item);
    }

    std::sort(range.m_items.begin(), range.m_items.end(), [](const Item &a, const Item &b) {
        if (a.from != b.from) {
            return a.from < b.from;
        }
        // An open end sorts after any bounded end.
        return a.to.has_value() && (!b.to.has_value() || *a.to < *b.to);
    });
    return range;
}

bool VolumeRange::contains(double number) const
{
    return std::any_of(m_items.cbegin(), m_items.cend(), [number](const Item &item) {
        return number >= item.from && (!item.to || number <= *item.to);
    });
}

QString VolumeRange::toString() const
{
    QStringList parts;
    for (const Item &item : m_items) {
        if (!item.to) {
            parts.append(formatNumber(item.from) + QLatin1Char('-'));
        } else if (*item.to == item.from) {
            parts.append(formatNumber(item.from));
        } else {
            parts.append(formatNumber(item.from) + QLatin1Char('-') + formatNumber(*item.to));
        }
    }
    return parts.join(QLatin1Char(','));
}

} // namespace Kobold
