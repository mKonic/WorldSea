#pragma once
// Fuzzy matching for quick open: the query's letters in order, scored up for
// a prefix, word starts and runs, and down for gaps.

#include "core/world.h"

#include <QString>

namespace worldsea {

// -1 when the letters are not all there in order; higher is better
int fuzzyScore(const QString &query, const QString &text);

struct SearchHit {
    Entity entity;
    int score = 0;
    QString matched; // the name or alias that matched
};

// items by name or alias, best first
QList<SearchHit> searchEntities(const World &w, const QString &query, int limit = 50);

} // namespace worldsea
