#include "core/search.h"

#include <algorithm>

namespace worldsea {

int fuzzyScore(const QString &query, const QString &text)
{
    const QString q = query.trimmed().toCaseFolded();
    const QString t = text.toCaseFolded();
    if (q.isEmpty()) return 0;
    if (t.startsWith(q)) return 1000 - int(t.size());
    if (const int at = t.indexOf(q); at >= 0) return 700 - at - int(t.size());

    int score = 0, ti = 0, run = 0;
    for (const QChar c : q) {
        if (c.isSpace()) continue;
        const int found = t.indexOf(c, ti);
        if (found < 0) return -1;
        const bool wordStart = found == 0 || !t[found - 1].isLetterOrNumber();
        run = found == ti ? run + 1 : 0;
        score += 10 + (wordStart ? 15 : 0) + run * 5 - std::min(found - ti, 10);
        ti = found + 1;
    }
    return score;
}

QList<SearchHit> searchEntities(const World &w, const QString &query, int limit)
{
    QList<SearchHit> hits;
    for (const auto &e : w.entities()) {
        SearchHit h{e, fuzzyScore(query, e.name), e.name};
        for (const auto &a : e.aliases) {
            // an alias match counts a little less than the name itself
            const int s = fuzzyScore(query, a) - 5;
            if (s > h.score) h = {e, s, a};
        }
        if (h.score >= 0) hits << h;
    }
    std::stable_sort(hits.begin(), hits.end(), [](const SearchHit &a, const SearchHit &b) { return a.score > b.score; });
    if (hits.size() > limit) hits.resize(limit);
    return hits;
}

} // namespace worldsea
