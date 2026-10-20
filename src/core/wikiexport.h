#pragma once
// A reader's wiki of the world as of one chapter: an index and a page per
// item, plain HTML in a folder. Nothing past that chapter gets in — items not
// yet in the story, later values, links that only form later, later history —
// and whole types (secrets, plot threads) can be left out.

#include "core/world.h"

#include <QSet>
#include <QString>

namespace worldsea {

struct WikiExport {
    Id asOf = 0;          // the chapter readers have reached (0: the start)
    QSet<Id> types;       // the types to include
    QString dir;          // written into; created when missing
};

// the page count, or -1 with *error set
int exportWiki(const World &w, const WikiExport &opt, QString *error = nullptr);

// the types a reader's wiki leaves out unless asked: secrets and plot threads
bool spoilerTypeByDefault(const QString &typeName);

} // namespace worldsea
