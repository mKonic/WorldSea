#pragma once
// The story laid out in lanes: chapters across, one lane per item of a type
// (characters, usually) from its first appearance, a dot where something
// about it changed, and markers for the events it takes part in. Click a
// chapter heading to move the cursor there, a marker or a lane to open it.

#include "core/world.h"

#include <QWidget>

class QComboBox;
class QScrollArea;

namespace worldsea {

class LanesCanvas;

class LanesView : public QWidget {
    Q_OBJECT
public:
    explicit LanesView(World *world, QWidget *parent = nullptr);

signals:
    void openEntity(Id id);

private:
    void fillTypes();
    void rebuild();

    World *m_world;
    QComboBox *m_lanes, *m_markers;
    LanesCanvas *m_canvas;
};

} // namespace worldsea
