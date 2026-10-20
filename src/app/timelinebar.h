#pragma once
// The strip along the top: the chapter the world is seen as of (a slider
// over every chapter of every book, with steps either side), and how edits
// land on the timeline.

#include "core/world.h"

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QToolButton;

namespace worldsea {

class TimelineBar : public QWidget {
    Q_OBJECT
public:
    explicit TimelineBar(World *world, QWidget *parent = nullptr);

private:
    void refresh();
    World *m_world;
    QToolButton *m_prev, *m_next;
    QSlider *m_slider;
    QLabel *m_label;
    QPushButton *m_correct, *m_change;
};

} // namespace worldsea
