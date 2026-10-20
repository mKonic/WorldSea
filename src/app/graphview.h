#pragma once
// Every item in the story as of the cursor, and the links that hold then,
// as a web: drag items about (positions are kept), select one to light up
// its neighbours, double-click to open its sheet.

#include "core/layout.h"
#include "core/world.h"

#include <QGraphicsView>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QWidget>

class QCheckBox;
class QGraphicsScene;
class QToolButton;

namespace worldsea {

class GraphNode;
class GraphEdge;

class GraphCanvas : public QGraphicsView {
    Q_OBJECT
public:
    using QGraphicsView::QGraphicsView;
    // zoomed or panned by hand: stop fitting it on resize
    bool touched = false;

signals:
    void resized();
    void zoomed();

protected:
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
};

class GraphView : public QWidget {
    Q_OBJECT
public:
    explicit GraphView(World *world, QWidget *parent = nullptr);
    // after the scene has its first size
    void fitAll();

signals:
    void openEntity(Id id);

protected:
    void showEvent(QShowEvent *e) override;

private:
    friend class GraphNode;
    void rebuild();
    void arrange();
    void tick();
    void rescaleText();
    void selectionChanged();
    void nodeMoved(GraphNode *n);
    void nodeReleased(GraphNode *n);

    World *m_world;
    QGraphicsScene *m_scene;
    GraphCanvas *m_view;
    QToolButton *m_typesButton;
    QCheckBox *m_labels, *m_hideLonely;
    QSet<Id> m_hiddenTypes;
    QHash<Id, GraphNode *> m_nodes;
    QList<GraphEdge *> m_edges;
    ForceLayout m_layout;
    QTimer m_timer;
    int m_rounds = 0;
    bool m_rebuilding = false;
    bool m_fitted = false;
};

} // namespace worldsea
