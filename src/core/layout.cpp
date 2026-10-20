#include "core/layout.h"

#include <QLineF>

#include <cmath>

namespace worldsea {

void ForceLayout::seed(const QList<Id> &nodes)
{
    QHash<Id, QList<Id>> neighbours;
    for (const auto &[a, b] : edges) {
        neighbours[a] << b;
        neighbours[b] << a;
    }
    // loose newcomers ring the ones already placed, not the scene's origin
    QPointF center;
    if (!pos.isEmpty()) {
        for (const auto &p : pos) center += p;
        center /= double(pos.size());
    }
    int ring = 0;
    for (Id n : nodes) {
        if (pos.contains(n)) continue;
        QPointF at;
        bool placed = false;
        for (Id m : neighbours.value(n)) {
            if (!pos.contains(m)) continue;
            // a fixed spread around the neighbour, so nothing starts stacked
            const double a = double((quint64(n) * 2654435761u) % 6283) / 1000.0;
            at = pos[m] + QPointF(std::cos(a), std::sin(a)) * spacing * 0.8;
            placed = true;
            break;
        }
        if (!placed) {
            const double a = ring * 2.399963; // golden angle
            const double r = spacing * (1.0 + 0.5 * std::sqrt(double(ring)));
            at = center + QPointF(std::cos(a) * r, std::sin(a) * r);
            ++ring;
        }
        pos.insert(n, at);
    }
    if (temperature <= 0) temperature = spacing;
}

double ForceLayout::step()
{
    const QList<Id> nodes = pos.keys();
    QHash<Id, QPointF> disp;
    const double k = spacing;
    for (int i = 0; i < nodes.size(); ++i) {
        for (int j = i + 1; j < nodes.size(); ++j) {
            QPointF d = pos[nodes[i]] - pos[nodes[j]];
            double len = std::hypot(d.x(), d.y());
            if (len < 0.01) {
                d = QPointF(0.5 + i % 3, 0.5 + j % 3);
                len = std::hypot(d.x(), d.y());
            }
            // far apart, they leave each other be: otherwise an item with no
            // links is pushed off by everything and gravity never wins
            if (len > 2.5 * k) continue;
            const QPointF push = d / len * (k * k / len);
            disp[nodes[i]] += push;
            disp[nodes[j]] -= push;
        }
    }
    for (const auto &[a, b] : edges) {
        if (!pos.contains(a) || !pos.contains(b) || a == b) continue;
        const QPointF d = pos[a] - pos[b];
        const double len = std::max(0.01, std::hypot(d.x(), d.y()));
        const QPointF pull = d / len * (len * len / k);
        disp[a] -= pull;
        disp[b] += pull;
    }
    QPointF middle;
    for (Id n : nodes) middle += pos[n];
    if (!nodes.isEmpty()) middle /= double(nodes.size());
    QSet<Id> linked;
    for (const auto &[a, b] : edges) linked << a << b;
    double moved = 0;
    for (Id n : nodes) {
        if (pinned.contains(n)) continue;
        // a pull to the middle of them all keeps islands from drifting off;
        // items with no links have nothing else holding them, so pull harder
        QPointF d = disp.value(n) + (middle - pos[n]) * (linked.contains(n) ? 0.03 : 0.3);
        const double len = std::hypot(d.x(), d.y());
        if (len > temperature) d = d / len * temperature;
        pos[n] += d;
        moved = std::max(moved, std::hypot(d.x(), d.y()));
    }
    temperature = std::max(1.0, temperature * 0.985);
    return moved;
}

void ForceLayout::run(int rounds)
{
    for (int i = 0; i < rounds; ++i)
        if (step() < 0.5) break;
}

} // namespace worldsea
