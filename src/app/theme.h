#pragma once
// NeoSea's night palette: a dark room, gold for what is active.

#include <QColor>
#include <QString>

namespace worldsea {

struct Theme {
    QColor bg{"#191919"}, bgSoft{"#222222"}, pane{"#202020"}, raised{"#2a2a2a"};
    QColor accent{"#c9a86a"}, muted{"#8a8a8a"}, faint{"#5c5c5c"}, red{"#c0392b"};
    QColor line{"#3a3a3a"}, text{"#dddddd"};
    // the application style sheet
    QString styleSheet() const;
};

const Theme &theme();

// a type's color, or the accent when it has none
QColor typeColor(const QString &color);

} // namespace worldsea
