#include "app/mapview.h"

#include "app/theme.h"

#include <QComboBox>
#include <QContextMenuEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace worldsea {

class PinItem : public QGraphicsItem {
public:
    PinItem(MapView *view, Id map, const Entity &e, const QColor &color, const QStringList &here)
        : m_view(view), m_map(map), m_id(e.id), m_name(e.name), m_color(color), m_here(here)
    {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemIgnoresTransformations);
        setCursor(Qt::PointingHandCursor);
        setZValue(2);
        setToolTip(here.isEmpty() ? e.name : e.name + "\n" + here.join('\n'));
    }
    Id id() const { return m_id; }

    QRectF boundingRect() const override { return QRectF(-10, -26, 260, 48 + 16.0 * std::min<qsizetype>(m_here.size(), 4)); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        p->setRenderHint(QPainter::Antialiasing);
        // a drop-shaped marker with its point on the spot
        QPainterPath drop;
        drop.moveTo(0, 0);
        drop.cubicTo(-9, -10, -9, -22, 0, -22);
        drop.cubicTo(9, -22, 9, -10, 0, 0);
        p->setPen(QPen(isSelected() ? theme().text : QColor("#111111"), 1.5));
        p->setBrush(m_color);
        p->drawPath(drop);
        p->setBrush(QColor("#111111"));
        p->setPen(Qt::NoPen);
        p->drawEllipse(QPointF(0, -15), 3, 3);

        QFont f;
        f.setPixelSize(13);
        f.setBold(true);
        p->setFont(f);
        const QFontMetricsF fm(f);
        QRectF label(12, -22, fm.horizontalAdvance(m_name) + 12, fm.height() + 4);
        p->setBrush(QColor(25, 25, 25, 215));
        p->drawRoundedRect(label, 5, 5);
        p->setPen(theme().text);
        p->drawText(label, Qt::AlignCenter, m_name);

        QFont small;
        small.setPixelSize(12);
        p->setFont(small);
        const QFontMetricsF sm(small);
        double y = label.bottom() + 3;
        QStringList lines = m_here.mid(0, 4);
        if (m_here.size() > 4) lines[3] = QStringLiteral("+%1 more").arg(m_here.size() - 3);
        for (const auto &line : lines) {
            QRectF r(12, y, sm.horizontalAdvance(line) + 10, sm.height() + 2);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(25, 25, 25, 190));
            p->drawRoundedRect(r, 4, 4);
            p->setPen(theme().muted);
            p->drawText(r, Qt::AlignCenter, line);
            y += 16;
        }
    }

protected:
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override
    {
        QGraphicsItem::mouseReleaseEvent(e);
        if (e->buttonDownScenePos(Qt::LeftButton) != e->scenePos()) {
            // the rebuild that follows replaces this item
            QMetaObject::invokeMethod(m_view, [w = m_view->m_world, map = m_map, id = m_id, at = pos()] { w->setPin(map, id, at); },
                                      Qt::QueuedConnection);
        }
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *) override { emit m_view->openEntity(m_id); }
    void contextMenuEvent(QGraphicsSceneContextMenuEvent *e) override
    {
        QMenu menu;
        menu.addAction("Open", [this] { emit m_view->openEntity(m_id); });
        menu.addAction("Remove pin", [w = m_view->m_world, map = m_map, id = m_id] { w->removePin(map, id); });
        menu.exec(e->screenPos());
    }

private:
    MapView *m_view;
    Id m_map, m_id;
    QString m_name;
    QColor m_color;
    QStringList m_here;
};

void MapCanvas::wheelEvent(QWheelEvent *e)
{
    const double f = std::pow(1.0015, e->angleDelta().y());
    const double now = transform().m11();
    if ((now < 0.05 && f < 1) || (now > 8 && f > 1)) return;
    touched = true;
    scale(f, f);
}

void MapCanvas::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    emit resized();
}

void MapCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) touched = true;
    QGraphicsView::mousePressEvent(e);
}

void MapCanvas::contextMenuEvent(QContextMenuEvent *e)
{
    if (itemAt(e->pos()) && !dynamic_cast<QGraphicsPixmapItem *>(itemAt(e->pos()))) {
        QGraphicsView::contextMenuEvent(e);
        return;
    }
    emit menuAt(mapToScene(e->pos()), e->globalPos());
}

MapView::MapView(World *world, QWidget *parent) : QWidget(parent), m_world(world)
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *bar = new QWidget;
    bar->setObjectName("timeline");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(14, 8, 14, 8);
    auto *label = new QLabel("Map");
    label->setObjectName("muted");
    m_map = new QComboBox;
    m_map->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto *hint = new QLabel("Right-click to pin · drag a pin to move it · double-click to open · scroll to zoom");
    hint->setObjectName("muted");
    row->addWidget(label);
    row->addWidget(m_map);
    row->addStretch();
    row->addWidget(hint);
    col->addWidget(bar);

    m_empty = new QLabel("No maps yet. Give a location an image in its Map field (or any item an image), "
                         "and it shows up here.");
    m_empty->setObjectName("muted");
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    col->addWidget(m_empty, 1);

    m_scene = new QGraphicsScene(this);
    m_scene->setBackgroundBrush(theme().bg);
    m_view = new MapCanvas(m_scene);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    col->addWidget(m_view, 1);

    connect(m_map, &QComboBox::activated, this, [this] {
        m_shownImage.clear();
        rebuild();
    });
    connect(m_view, &MapCanvas::menuAt, this, &MapView::menuAt);
    connect(m_view, &MapCanvas::resized, this, [this] {
        if (!m_view->touched) fit();
    });
    connect(world, &World::changed, this, [this] {
        fillMaps();
        rebuild();
    });
    connect(world, &World::cursorChanged, this, [this] {
        fillMaps();
        rebuild();
    });
    fillMaps();
    rebuild();
}

Id MapView::mapEntity() const { return m_map->currentData().toLongLong(); }

void MapView::fillMaps()
{
    const Id keep = mapEntity();
    QSignalBlocker b(m_map);
    m_map->clear();
    // every item with an image as of the cursor; locations first
    QList<std::pair<int, Entity>> withImage;
    const auto types = m_world->types();
    for (int ti = 0; ti < types.size(); ++ti) {
        QList<Id> imageFields;
        for (const auto &f : m_world->fields(types[ti].id))
            if (f.kind == Kind::Image) imageFields << f.id;
        if (imageFields.isEmpty()) continue;
        const int rank = types[ti].name.compare("Location", Qt::CaseInsensitive) == 0 ? 0 : 1;
        for (const auto &e : m_world->entities(types[ti].id)) {
            for (Id f : imageFields) {
                if (!m_world->effective(e.id, f).value.isEmpty()) {
                    withImage.append({rank, e});
                    break;
                }
            }
        }
    }
    std::stable_sort(withImage.begin(), withImage.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &[rank, e] : withImage) m_map->addItem(e.name, e.id);
    m_map->setCurrentIndex(std::max(0, m_map->findData(keep)));
}

void MapView::rebuild()
{
    const Id map = mapEntity();
    m_empty->setVisible(!map);
    m_view->setVisible(map);
    const auto me = m_world->entity(map);
    if (!me) {
        m_scene->clear();
        return;
    }
    QString imageValue;
    for (const auto &f : m_world->fields(me->type)) {
        if (f.kind != Kind::Image) continue;
        imageValue = m_world->effective(map, f.id).value;
        if (!imageValue.isEmpty()) break;
    }
    const QPointF center = m_view->mapToScene(m_view->viewport()->rect().center());
    const QTransform zoom = m_view->transform();
    m_scene->clear();
    QPixmap pm;
    pm.loadFromData(m_world->blob(imageValue.toLongLong()));
    auto *bg = m_scene->addPixmap(pm);
    bg->setZValue(0);
    m_scene->setSceneRect(QRectF(pm.rect()).adjusted(-200, -200, 200, 200));

    const Id cursor = m_world->cursor();
    QHash<Id, QString> colors;
    QHash<Id, QString> typeNames;
    for (const auto &t : m_world->types()) {
        colors.insert(t.id, t.color);
        typeNames.insert(t.id, t.name);
    }
    const auto links = m_world->allLinks(cursor);
    for (const auto &pin : m_world->pins(map)) {
        const auto e = m_world->entity(pin.entity);
        if (!e || !m_world->existsAt(*e, cursor)) continue;
        // who and what is at this place now, through any link to it
        QStringList here;
        for (const auto &l : links) {
            const Id other = l.dst == e->id ? l.src : l.src == e->id ? l.dst : 0;
            if (!other) continue;
            const auto o = m_world->entity(other);
            if (!o || o->type == e->type || !m_world->existsAt(*o, cursor) || here.contains(o->name)) continue;
            here << o->name;
        }
        auto *item = new PinItem(this, map, *e, typeColor(colors.value(e->type)), here);
        item->setPos(pin.at);
        m_scene->addItem(item);
    }

    if (m_shownImage != imageValue) {
        m_shownImage = imageValue;
        m_view->touched = false;
        fit();
    } else {
        m_view->setTransform(zoom);
        m_view->centerOn(center);
    }
}

void MapView::fit()
{
    for (auto *it : m_scene->items())
        if (auto *bg = dynamic_cast<QGraphicsPixmapItem *>(it)) m_view->fitInView(bg->boundingRect(), Qt::KeepAspectRatio);
}

void MapView::menuAt(QPointF scenePos, QPoint globalPos)
{
    const Id map = mapEntity();
    if (!map) return;
    QSet<Id> pinned;
    for (const auto &p : m_world->pins(map)) pinned.insert(p.entity);
    QMenu menu;
    auto *title = menu.addAction("Pin here");
    title->setEnabled(false);
    for (const auto &t : m_world->types()) {
        QMenu *sub = nullptr;
        for (const auto &e : m_world->entities(t.id)) {
            if (e.id == map || pinned.contains(e.id)) continue;
            if (!sub) sub = menu.addMenu(t.name);
            sub->addAction(e.name, this, [this, map, id = e.id, scenePos] { m_world->setPin(map, id, scenePos); });
        }
    }
    menu.exec(globalPos);
}

} // namespace worldsea

#include "moc_mapview.cpp"
