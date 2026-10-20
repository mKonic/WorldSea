#pragma once
// An item's image as a map: other items pinned on it where the writer put
// them, and beside each pin whoever is linked to that place as of the
// cursor (who lives there, what happens there). Right-click to pin, drag a
// pin to move it, double-click it to open.

#include "core/world.h"

#include <QGraphicsView>
#include <QWidget>

class QComboBox;
class QGraphicsPixmapItem;
class QGraphicsScene;
class QLabel;

namespace worldsea {

class MapCanvas : public QGraphicsView {
    Q_OBJECT
public:
    using QGraphicsView::QGraphicsView;
    // zoomed or panned by hand: stop fitting it on resize
    bool touched = false;

signals:
    void menuAt(QPointF scenePos, QPoint globalPos);
    void resized();

protected:
    void wheelEvent(QWheelEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
};

class MapView : public QWidget {
    Q_OBJECT
public:
    explicit MapView(World *world, QWidget *parent = nullptr);

signals:
    void openEntity(Id id);

private:
    friend class PinItem;
    void fillMaps();
    void rebuild();
    void menuAt(QPointF scenePos, QPoint globalPos);
    Id mapEntity() const;
    void fit();

    World *m_world;
    QComboBox *m_map;
    QLabel *m_empty;
    QGraphicsScene *m_scene;
    MapCanvas *m_view;
    QString m_shownImage; // the image the scene holds, to refit only when it changes
};

} // namespace worldsea
