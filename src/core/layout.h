#pragma once
// Force-directed placement for the relationship graph (Fruchterman-Reingold
// with gravity): linked items pull together, everything pushes apart.

#include "core/world.h"

#include <QHash>
#include <QPointF>
#include <QSet>

namespace worldsea {

struct ForceLayout {
    // positions in and out; nodes in `pinned` keep theirs
    QHash<Id, QPointF> pos;
    QSet<Id> pinned;
    QList<std::pair<Id, Id>> edges;
    double spacing = 190; // the ideal distance between linked items
    double temperature = 0; // 0: start from the spacing

    // nodes without a position start near a linked neighbour, or on a ring
    void seed(const QList<Id> &nodes);
    // one round; returns how far the furthest node moved
    double step();
    void run(int rounds);
};

} // namespace worldsea
