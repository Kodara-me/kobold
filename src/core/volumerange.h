// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QString>

#include <optional>

namespace Kobold
{

// Volume range of a followed series, e.g. "1-3,7,10-" (SPEC-SELECTION.md §4).
class VolumeRange
{
public:
    // Returns std::nullopt for an invalid range, including an empty one.
    static std::optional<VolumeRange> parse(const QString &text);

    // True when number falls in at least one item.
    bool contains(double number) const;

    // Normalized text: items sorted, no spaces.
    QString toString() const;

private:
    struct Item {
        double from = 0;
        // Absent for "N-" (to the end).
        std::optional<double> to;
    };

    QList<Item> m_items;
};

} // namespace Kobold
