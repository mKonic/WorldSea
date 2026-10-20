#pragma once
// Continuity checks: things recorded along the timeline that contradict each
// other. Each issue names the item, the chapter it shows up in, and what is
// wrong; some are mistakes (a link ending before it starts), some only worth
// a look (a cultivation realm going down, which a story may intend).

#include "core/world.h"

namespace worldsea {

struct Issue {
    enum Severity { Warning, Note } severity = Warning;
    Id entity = 0;
    Id chapter = 0; // where to look; 0 = the start
    Id field = 0;
    QString text;
};

// The words that mark a "Status" value as dead
bool readsAsDead(const QString &status);

QList<Issue> checkContinuity(const World &w);

} // namespace worldsea
