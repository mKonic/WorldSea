#include "app/graphview.h"

#include "app/theme.h"

#include <QCheckBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <cmath>

namespace worldsea {

namespace {
constexpr double kRadius = 11;
// labels grow in the scene as the view zooms out, so they keep a readable
// size on screen
double g_textScale = 1;
}

class GraphNode : public QGraphicsItem {
public:
    GraphNode(GraphView *view, const Entity &e, const QColor &color) : m_view(view), m_id(e.id), m_name(e.name), m_color(color)
    {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setAcceptHoverEvents(true);
        setCursor(Qt::PointingHandCursor);
        setZValue(2);
        setToolTip(e.aliases.isEmpty() ? e.name : e.name + "\nAlso: " + e.aliases.join(", "));
        m_stub = e.stub;
    }
    Id id() const { return m_id; }
    void rescaled()
    {
        prepareGeometryChange();
        update();
    }
    void setFaded(bool f)
    {
        m_faded = f;
        update();
    }

    QRectF boundingRect() const override
    {
        const QFontMetricsF fm(font());
        const double w = std::max(2 * kRadius, fm.horizontalAdvance(m_name)) + 8;
        return QRectF(-w / 2, -kRadius - 2, w, 2 * kRadius + 6 + fm.height());
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        p->setRenderHint(QPainter::Antialiasing);
        p->setOpacity(m_faded ? 0.22 : 1.0);
        const bool hot = isSelected() || m_hover;
        QColor fill = m_color;
        if (m_stub) fill.setAlphaF(0.25);
        p->setPen(QPen(hot ? theme().text : m_color.darker(130), hot ? 2 : 1));
        p->setBrush(fill);
        p->drawEllipse(QPointF(0, 0), kRadius, kRadius);
        p->setFont(font());
        p->setPen(hot ? theme().text : theme().muted);
        const QFontMetricsF fm(font());
        const double w = fm.horizontalAdvance(m_name);
        p->drawText(QPointF(-w / 2, kRadius + 4 + fm.ascent()), m_name);
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override
    {
        if (change == ItemPositionHasChanged) m_view->nodeMoved(this);
        if (change == ItemSelectedHasChanged) m_view->selectionChanged();
        return QGraphicsItem::itemChange(change, value);
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override
    {
        QGraphicsItem::mouseReleaseEvent(e);
        m_view->nodeReleased(this);
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *) override { emit m_view->openEntity(m_id); }
    void hoverEnterEvent(QGraphicsSceneHoverEvent *) override
    {
        m_hover = true;
        update();
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override
    {
        m_hover = false;
        update();
    }

private:
    static QFont font()
    {
        QFont f;
        f.setPixelSize(int(13 * g_textScale));
        return f;
    }
    GraphView *m_view;
    Id m_id;
    QString m_name;
    QColor m_color;
    bool m_stub = false, m_faded = false, m_hover = false;
};

class GraphEdge : public QGraphicsItem {
public:
    GraphEdge(GraphNode *a, GraphNode *b) : m_a(a), m_b(b) { setZValue(1); }
    GraphNode *a() const { return m_a; }
    GraphNode *b() const { return m_b; }
    // field names read from a to b, and from b to a
    QStringList forward, backward;
    bool symmetric = false;
    bool showLabel = true;

    void setFaded(bool f, bool hot)
    {
        m_faded = f;
        m_hot = hot;
        update();
    }
    void adjust()
    {
        prepareGeometryChange();
        m_line = QLineF(m_a->pos(), m_b->pos());
        setToolTip(label(true));
    }
    QString label(bool full) const
    {
        QStringList parts;
        if (!forward.isEmpty()) parts << forward.join(", ");
        if (!backward.isEmpty()) parts << backward.join(", ");
        Q_UNUSED(full);
        return parts.join(" / ");
    }

    QRectF boundingRect() const override
    {
        return QRectF(m_line.p1(), m_line.p2()).normalized().adjusted(-80 * g_textScale, -20 * g_textScale, 80 * g_textScale,
                                                                       20 * g_textScale);
    }
    QPainterPath shape() const override
    {
        QPainterPath path(m_line.p1());
        path.lineTo(m_line.p2());
        QPainterPathStroker s;
        s.setWidth(8);
        return s.createStroke(path);
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        if (m_line.length() < 2 * kRadius) return;
        p->setRenderHint(QPainter::Antialiasing);
        p->setOpacity(m_faded ? 0.12 : 1.0);
        const QColor c = m_hot ? theme().accent : theme().line.lighter(140);
        p->setPen(QPen(c, m_hot ? 1.8 : 1.2));
        // stop at the rims
        const QPointF dir = (m_line.p2() - m_line.p1()) / m_line.length();
        const QPointF from = m_line.p1() + dir * kRadius;
        const QPointF to = m_line.p2() - dir * kRadius;
        p->drawLine(from, to);
        if (!symmetric) {
            auto arrow = [&](QPointF tip, QPointF d) {
                const QPointF n(-d.y(), d.x());
                QPolygonF head{tip, tip - d * 8 + n * 4, tip - d * 8 - n * 4};
                p->setBrush(c);
                p->drawPolygon(head);
            };
            if (!forward.isEmpty()) arrow(to, dir);
            if (!backward.isEmpty()) arrow(from, -dir);
        }
        if (showLabel || m_hot) {
            QFont f;
            f.setPixelSize(int(11 * g_textScale));
            p->setFont(f);
            p->setPen(m_hot ? theme().text : theme().muted);
            const QString text = label(false);
            const QFontMetricsF fm(f);
            const QPointF mid = (from + to) / 2;
            const QRectF box(mid.x() - fm.horizontalAdvance(text) / 2 - 3, mid.y() - fm.height() / 2,
                             fm.horizontalAdvance(text) + 6, fm.height());
            p->fillRect(box, theme().bg);
            p->drawText(box, Qt::AlignCenter, text);
        }
    }

private:
    GraphNode *m_a, *m_b;
    QLineF m_line;
    bool m_faded = false, m_hot = false;
};

void GraphCanvas::wheelEvent(QWheelEvent *e)
{
    const double f = std::pow(1.0015, e->angleDelta().y());
    const double now = transform().m11();
    if ((now < 0.15 && f < 1) || (now > 4 && f > 1)) return;
    touched = true;
    scale(f, f);
    emit zoomed();
}

void GraphCanvas::resizeEvent(QResizeEvent *e)
{
    QGraphicsView::resizeEvent(e);
    emit resized();
}

void GraphCanvas::mousePressEvent(QMouseEvent *e)
{
    touched = true;
    QGraphicsView::mousePressEvent(e);
}

GraphView::GraphView(World *world, QWidget *parent) : QWidget(parent), m_world(world)
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto *bar = new QWidget;
    bar->setObjectName("timeline");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(12, 6, 12, 6);
    m_typesButton = new QToolButton;
    m_typesButton->setText("Types ▾");
    m_typesButton->setPopupMode(QToolButton::InstantPopup);
    auto *typesMenu = new QMenu(m_typesButton);
    connect(typesMenu, &QMenu::aboutToShow, this, [this, typesMenu] {
        typesMenu->clear();
        for (const auto &t : m_world->types()) {
            auto *a = typesMenu->addAction(t.name);
            a->setCheckable(true);
            a->setChecked(!m_hiddenTypes.contains(t.id));
            connect(a, &QAction::toggled, this, [this, id = t.id](bool on) {
                if (on) m_hiddenTypes.remove(id);
                else m_hiddenTypes.insert(id);
                rebuild();
            });
        }
    });
    m_typesButton->setMenu(typesMenu);
    m_labels = new QCheckBox("Link names");
    m_labels->setChecked(true);
    m_hideLonely = new QCheckBox("Hide unlinked");
    auto *arrangeButton = new QPushButton("Arrange");
    arrangeButton->setToolTip("Lay everything out again");
    auto *fit = new QPushButton("Fit");
    auto *hint = new QLabel("Drag to move · double-click to open · scroll to zoom");
    hint->setObjectName("muted");
    row->addWidget(m_typesButton);
    row->addWidget(m_labels);
    row->addWidget(m_hideLonely);
    row->addStretch();
    row->addWidget(hint);
    row->addSpacing(12);
    row->addWidget(fit);
    row->addWidget(arrangeButton);
    col->addWidget(bar);

    m_scene = new QGraphicsScene(this);
    m_scene->setBackgroundBrush(theme().bg);
    m_view = new GraphCanvas(m_scene);
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setDragMode(QGraphicsView::ScrollHandDrag);
    m_view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    m_view->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    col->addWidget(m_view, 1);

    connect(m_labels, &QCheckBox::toggled, this, [this](bool on) {
        for (auto *e : m_edges) {
            e->showLabel = on;
            e->update();
        }
    });
    connect(m_hideLonely, &QCheckBox::toggled, this, &GraphView::rebuild);
    connect(arrangeButton, &QPushButton::clicked, this, &GraphView::arrange);
    connect(fit, &QPushButton::clicked, this, [this] {
        m_view->touched = false;
        fitAll();
    });
    connect(m_view, &GraphCanvas::zoomed, this, &GraphView::rescaleText);
    connect(m_view, &GraphCanvas::resized, this, [this] {
        if (!m_view->touched) fitAll();
    });
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, &GraphView::tick);
    connect(world, &World::changed, this, &GraphView::rebuild);
    connect(world, &World::cursorChanged, this, &GraphView::rebuild);
    rebuild();
}

void GraphView::rescaleText()
{
    const double zoom = m_view->transform().m11();
    const double s = std::clamp(zoom < 1 ? 1 / zoom : 1.0, 1.0, 4.0);
    if (std::abs(s - g_textScale) < 0.01) return;
    g_textScale = s;
    for (auto *n : m_nodes) n->rescaled();
    for (auto *e : m_edges) e->adjust();
}

void GraphView::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (!m_fitted && !m_nodes.isEmpty()) {
        m_fitted = true;
        // once the view has its size
        QMetaObject::invokeMethod(this, &GraphView::fitAll, Qt::QueuedConnection);
    }
}

void GraphView::fitAll()
{
    // by where the items are, not by their labels, which grow as it zooms out
    QRectF r;
    for (auto *n : m_nodes) r |= QRectF(n->pos(), QSizeF(1, 1));
    if (m_nodes.isEmpty()) return;
    r.adjust(-120, -70, 120, 90);
    m_view->fitInView(r, Qt::KeepAspectRatio);
    if (m_view->transform().m11() > 1.4) {
        m_view->resetTransform();
        m_view->centerOn(r.center());
    }
    rescaleText();
}

void GraphView::rebuild()
{
    if (!m_world->isOpen()) return;
    m_rebuilding = true;
    m_timer.stop();
    Id selected = 0;
    for (auto *it : m_scene->selectedItems())
        if (auto *n = dynamic_cast<GraphNode *>(it)) selected = n->id();
    m_scene->clear();
    m_nodes.clear();
    m_edges.clear();

    const Id cursor = m_world->cursor();
    QHash<Id, Entity> shown;
    for (const auto &e : m_world->entities()) {
        if (m_hiddenTypes.contains(e.type) || !m_world->existsAt(e, cursor)) continue;
        shown.insert(e.id, e);
    }

    // one edge per pair, carrying every field that links them
    struct Pair {
        QStringList forward, backward;
        bool symmetric = true;
    };
    QHash<std::pair<Id, Id>, Pair> pairs;
    QHash<Id, FieldDef> fieldCache;
    QSet<Id> linked;
    for (const auto &l : m_world->allLinks(cursor)) {
        if (!shown.contains(l.src) || !shown.contains(l.dst) || l.src == l.dst) continue;
        if (!fieldCache.contains(l.field)) {
            if (const auto f = m_world->field(l.field)) fieldCache.insert(l.field, *f);
        }
        const FieldDef f = fieldCache.value(l.field);
        // read it the way the link's source sheet shows it: Elder Mo → Disciples → Lin
        const bool flip = l.src > l.dst;
        const auto key = flip ? std::pair{l.dst, l.src} : std::pair{l.src, l.dst};
        Pair &p = pairs[key];
        QStringList &side = flip ? p.backward : p.forward;
        if (!side.contains(f.name)) side << f.name;
        if (f.inverse != f.id) p.symmetric = false;
        linked << l.src << l.dst;
    }

    const auto saved = m_world->nodePositions();
    m_layout = ForceLayout{};
    QList<Id> order;
    for (auto it = shown.begin(); it != shown.end(); ++it) {
        if (m_hideLonely->isChecked() && !linked.contains(it.key())) continue;
        order << it.key();
        if (saved.contains(it.key())) {
            m_layout.pos.insert(it.key(), saved[it.key()]);
            m_layout.pinned.insert(it.key());
        }
    }
    std::sort(order.begin(), order.end());
    for (auto it = pairs.begin(); it != pairs.end(); ++it) m_layout.edges << it.key();
    const bool fresh = order.size() != m_layout.pinned.size();
    m_layout.seed(order);
    if (fresh) {
        // settle the newcomers around the ones already placed
        m_layout.temperature = m_layout.spacing / 2;
        m_layout.run(m_layout.pinned.isEmpty() ? 300 : 120);
        for (Id id : order)
            if (!saved.contains(id)) m_world->setNodePosition(id, m_layout.pos[id]);
    }

    for (Id id : order) {
        const Entity &e = shown[id];
        const auto t = m_world->type(e.type);
        auto *n = new GraphNode(this, e, typeColor(t ? t->color : QString()));
        n->setPos(m_layout.pos[id]);
        m_scene->addItem(n);
        m_nodes.insert(id, n);
    }
    for (auto it = pairs.begin(); it != pairs.end(); ++it) {
        auto *a = m_nodes.value(it.key().first);
        auto *b = m_nodes.value(it.key().second);
        if (!a || !b) continue;
        auto *edge = new GraphEdge(a, b);
        edge->forward = it->forward;
        edge->backward = it->backward;
        edge->symmetric = it->symmetric;
        edge->showLabel = m_labels->isChecked();
        edge->adjust();
        m_scene->addItem(edge);
        m_edges << edge;
    }
    if (auto *n = m_nodes.value(selected)) n->setSelected(true);
    m_rebuilding = false;
    selectionChanged();
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-2000, -2000, 2000, 2000));
    if (!m_view->touched && isVisible()) fitAll();
}

void GraphView::arrange()
{
    m_layout.pinned.clear();
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) m_layout.pos[it.key()] = it.value()->pos();
    m_layout.temperature = m_layout.spacing;
    m_rounds = 0;
    m_timer.start();
}

void GraphView::tick()
{
    const double moved = m_layout.step();
    m_rebuilding = true;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) it.value()->setPos(m_layout.pos[it.key()]);
    m_rebuilding = false;
    for (auto *e : m_edges) e->adjust();
    if (++m_rounds > 240 || moved < 0.3) {
        m_timer.stop();
        for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) m_world->setNodePosition(it.key(), it.value()->pos());
        fitAll();
    }
}

void GraphView::nodeMoved(GraphNode *n)
{
    for (auto *e : m_edges)
        if (e->a() == n || e->b() == n) e->adjust();
}

void GraphView::nodeReleased(GraphNode *n)
{
    if (!m_rebuilding) m_world->setNodePosition(n->id(), n->pos());
}

void GraphView::selectionChanged()
{
    if (m_rebuilding) return;
    QSet<GraphNode *> focus;
    for (auto *it : m_scene->selectedItems())
        if (auto *n = dynamic_cast<GraphNode *>(it)) focus.insert(n);
    QSet<GraphNode *> lit = focus;
    for (auto *e : m_edges) {
        if (focus.contains(e->a())) lit.insert(e->b());
        if (focus.contains(e->b())) lit.insert(e->a());
    }
    for (auto *n : m_nodes) n->setFaded(!focus.isEmpty() && !lit.contains(n));
    for (auto *e : m_edges) {
        const bool hot = focus.contains(e->a()) || focus.contains(e->b());
        e->setFaded(!focus.isEmpty() && !hot, hot);
    }
}

} // namespace worldsea

#include "moc_graphview.cpp"
