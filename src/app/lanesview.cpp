#include "app/lanesview.h"

#include "app/fieldeditors.h"
#include "app/theme.h"
#include "core/continuity.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolTip>
#include <QVBoxLayout>

namespace worldsea {

namespace {
constexpr int kNameWidth = 170;
constexpr int kColWidth = 160;
constexpr int kHeadHeight = 44;
constexpr int kRowHeight = 38;
} // namespace

struct Lane {
    Entity entity;
    int from = 0;       // first column it is in the story
    int until = -1;     // column it dies in, -1 = never
    QHash<int, QStringList> changes; // column -> what changed
};

struct Marker {
    Id entity = 0; // the event
    QString name;
    QColor color;
    int column = 0;
    int lane = 0;
};

class LanesCanvas : public QWidget {
public:
    explicit LanesCanvas(LanesView *view) : m_view(view) { setMouseTracking(true); }
    QList<Id> chapters;
    QStringList titles;
    int cursorColumn = -1;
    QList<Lane> lanes;
    QList<Marker> markers;
    QColor laneColor;
    std::function<void(Id)> pickChapter;
    std::function<void(Id)> open;

    void relayout()
    {
        // room past the last chapter for its markers
        setFixedSize(kNameWidth + std::max(1, int(chapters.size())) * kColWidth + kColWidth / 2 + 20,
                     kHeadHeight + std::max(1, int(lanes.size())) * kRowHeight + 20);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const Theme &th = theme();
        p.fillRect(rect(), th.bg);
        QFont small = font();
        small.setPixelSize(11);
        QFont normal = font();
        normal.setPixelSize(13);

        // the cursor's column
        if (cursorColumn >= 0) {
            QColor c = th.accent;
            c.setAlphaF(0.07);
            p.fillRect(QRect(kNameWidth + cursorColumn * kColWidth, 0, kColWidth, height()), c);
        }
        // chapter headings and column rules
        p.setFont(small);
        for (int c = 0; c < chapters.size(); ++c) {
            const QRect head(kNameWidth + c * kColWidth + 6, 0, kColWidth - 12, kHeadHeight);
            p.setPen(c == cursorColumn ? th.accent : th.muted);
            p.drawText(head, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                       QFontMetrics(small).elidedText(titles[c], Qt::ElideRight, (kColWidth - 12) * 2));
            p.setPen(QColor("#242424"));
            p.drawLine(kNameWidth + c * kColWidth, kHeadHeight, kNameWidth + c * kColWidth, height());
        }
        p.setPen(th.line);
        p.drawLine(0, kHeadHeight, width(), kHeadHeight);

        for (int r = 0; r < lanes.size(); ++r) {
            const Lane &l = lanes[r];
            const int y = kHeadHeight + r * kRowHeight;
            const int mid = y + kRowHeight / 2;
            p.setFont(normal);
            p.setPen(l.entity.stub ? th.faint : th.text);
            p.drawText(QRect(12, y, kNameWidth - 18, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(normal).elidedText(l.entity.name, Qt::ElideRight, kNameWidth - 18));
            // the lane runs from its first appearance to its death or the end
            const int x0 = kNameWidth + l.from * kColWidth + kColWidth / 2;
            const int x1 = l.until >= 0 ? kNameWidth + l.until * kColWidth + kColWidth / 2
                                        : kNameWidth + int(chapters.size()) * kColWidth;
            QColor lane = laneColor;
            lane.setAlphaF(0.45);
            p.setPen(QPen(lane, 2));
            p.drawLine(x0, mid, x1, mid);
            if (l.until >= 0) {
                p.setPen(QPen(th.muted, 1.5));
                p.drawLine(x1 - 5, mid - 5, x1 + 5, mid + 5);
                p.drawLine(x1 + 5, mid - 5, x1 - 5, mid + 5);
            }
            for (auto it = l.changes.begin(); it != l.changes.end(); ++it) {
                const QPointF c(kNameWidth + it.key() * kColWidth + kColWidth / 2.0, mid);
                p.setPen(Qt::NoPen);
                p.setBrush(laneColor);
                const double rad = std::min(7.0, 3.5 + it.value().size());
                p.drawEllipse(c, rad, rad);
            }
            p.setPen(QColor("#242424"));
            p.drawLine(0, y + kRowHeight, width(), y + kRowHeight);
        }

        // events drawn over the lanes, beside the dot
        p.setFont(small);
        for (const auto &m : markers) {
            // from just right of the dot to just short of the next chapter's
            const int x = kNameWidth + m.column * kColWidth + kColWidth / 2 + 12;
            QRect r(x, 0, kColWidth - 34, 20);
            r.moveTop(kHeadHeight + m.lane * kRowHeight + (kRowHeight - 20) / 2);
            QColor fill = m.color;
            fill.setAlphaF(0.18);
            p.setPen(QPen(m.color, 1));
            p.setBrush(fill);
            p.drawRoundedRect(r, 10, 10);
            p.setPen(th.text);
            p.drawText(r.adjusted(7, 0, -5, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(small).elidedText(m.name, Qt::ElideRight, r.width() - 12));
        }
    }

    // what is under a point: a chapter heading, a marker or a lane cell
    struct Hit {
        int column = -1, lane = -1;
        const Marker *marker = nullptr;
    };
    Hit hit(QPoint pos) const
    {
        Hit h;
        if (pos.x() >= kNameWidth) h.column = (pos.x() - kNameWidth) / kColWidth;
        if (h.column >= chapters.size()) h.column = -1;
        if (pos.y() >= kHeadHeight) h.lane = (pos.y() - kHeadHeight) / kRowHeight;
        if (h.lane >= lanes.size()) h.lane = -1;
        for (const auto &m : markers)
            if (m.lane == h.lane && m.column == h.column && pos.x() > kNameWidth + m.column * kColWidth + kColWidth / 2 + 8)
                h.marker = &m;
        return h;
    }

    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::ToolTip) {
            const auto *he = static_cast<QHelpEvent *>(e);
            const Hit h = hit(he->pos());
            QString tip;
            if (h.marker) {
                tip = h.marker->name;
            } else if (h.lane >= 0 && h.column >= 0) {
                const Lane &l = lanes[h.lane];
                tip = l.changes.value(h.column).join('\n');
            } else if (h.lane < 0 && h.column >= 0) {
                tip = titles[h.column] + "\nClick to view the world as of this chapter";
            }
            if (tip.isEmpty()) QToolTip::hideText();
            else QToolTip::showText(he->globalPos(), tip, this);
            return true;
        }
        return QWidget::event(e);
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        const Hit h = hit(e->pos());
        setCursor(h.marker || (h.lane < 0 && h.column >= 0) || (h.lane >= 0 && e->pos().x() < kNameWidth)
                      ? Qt::PointingHandCursor
                      : Qt::ArrowCursor);
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() != Qt::LeftButton) return;
        const Hit h = hit(e->pos());
        if (h.marker) open(h.marker->entity);
        else if (h.lane < 0 && h.column >= 0) pickChapter(chapters[h.column]);
        else if (h.lane >= 0 && h.column >= 0) {
            pickChapter(chapters[h.column]);
            open(lanes[h.lane].entity.id);
        } else if (h.lane >= 0) open(lanes[h.lane].entity.id);
    }

private:
    LanesView *m_view;
};

LanesView::LanesView(World *world, QWidget *parent) : QWidget(parent), m_world(world)
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *bar = new QWidget;
    bar->setObjectName("timeline");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(14, 8, 14, 8);
    auto *lanesLabel = new QLabel("Lanes");
    lanesLabel->setObjectName("muted");
    m_lanes = new QComboBox;
    auto *markersLabel = new QLabel("Markers");
    markersLabel->setObjectName("muted");
    m_markers = new QComboBox;
    auto *hint = new QLabel("Dots are changes · click a chapter to go there · click a marker or name to open");
    hint->setObjectName("muted");
    row->addWidget(lanesLabel);
    row->addWidget(m_lanes);
    row->addSpacing(12);
    row->addWidget(markersLabel);
    row->addWidget(m_markers);
    row->addStretch();
    row->addWidget(hint);
    col->addWidget(bar);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(false);
    m_canvas = new LanesCanvas(this);
    m_canvas->pickChapter = [this](Id ch) { m_world->setCursor(ch); };
    m_canvas->open = [this](Id id) { emit openEntity(id); };
    scroll->setWidget(m_canvas);
    col->addWidget(scroll, 1);

    connect(m_lanes, &QComboBox::activated, this, &LanesView::rebuild);
    connect(m_markers, &QComboBox::activated, this, &LanesView::rebuild);
    connect(world, &World::changed, this, [this] {
        fillTypes();
        rebuild();
    });
    connect(world, &World::cursorChanged, this, &LanesView::rebuild);
    fillTypes();
    rebuild();
}

void LanesView::fillTypes()
{
    const Id lanes = m_lanes->currentData().toLongLong();
    const Id markers = m_markers->currentData().toLongLong();
    QSignalBlocker a(m_lanes), b(m_markers);
    m_lanes->clear();
    m_markers->clear();
    m_markers->addItem("None", Id(0));
    Id character = 0, event = 0;
    for (const auto &t : m_world->types()) {
        m_lanes->addItem(t.name, t.id);
        m_markers->addItem(t.name, t.id);
        if (!character) character = t.id;
        if (!event && t.name.compare("Event", Qt::CaseInsensitive) == 0) event = t.id;
    }
    m_lanes->setCurrentIndex(std::max(0, m_lanes->findData(lanes ? lanes : character)));
    m_markers->setCurrentIndex(std::max(0, m_markers->findData(markers ? markers : event)));
}

void LanesView::rebuild()
{
    const Id laneType = m_lanes->currentData().toLongLong();
    const Id markerType = m_markers->currentData().toLongLong();
    const auto &timeline = m_world->timeline();
    QHash<Id, int> column;
    m_canvas->chapters = timeline;
    m_canvas->titles.clear();
    for (int i = 0; i < timeline.size(); ++i) {
        column.insert(timeline[i], i);
        const auto c = m_world->chapter(timeline[i]);
        m_canvas->titles << (c ? c->title : QString());
    }
    m_canvas->cursorColumn = column.value(m_world->cursor(), -1);
    const auto t = m_world->type(laneType);
    m_canvas->laneColor = typeColor(t ? t->color : QString());
    auto col = [&](Id ch) { return ch ? column.value(ch, 0) : 0; };

    QHash<Id, FieldDef> fields;
    if (t)
        for (const auto &f : m_world->fields(t->id)) fields.insert(f.id, f);
    m_canvas->lanes.clear();
    QHash<Id, int> laneOf;
    for (const auto &e : m_world->entities(laneType)) {
        Lane l;
        l.entity = e;
        l.from = col(e.intro);
        for (const auto &h : m_world->history(e.id)) {
            if (!h.chapter && h.what != HistoryEntry::Intro) continue; // from the start: not a change
            const auto f = fields.value(h.field);
            QString what;
            switch (h.what) {
            case HistoryEntry::Intro: what = "First appears"; break;
            case HistoryEntry::Value: what = f.name + " → " + displayValue(*m_world, f, h.value); break;
            case HistoryEntry::LinkStart:
            case HistoryEntry::LinkEnd: {
                const auto o = m_world->entity(h.other);
                const auto fd = m_world->field(h.field);
                what = QStringLiteral("%1 %2 %3").arg(fd ? fd->name : QString(), h.what == HistoryEntry::LinkStart ? "+" : "−",
                                                      o ? o->name : QString());
                break;
            }
            }
            l.changes[col(h.chapter)] << what;
            if (h.what == HistoryEntry::Value && f.kind == Kind::Text && f.name.compare("Status", Qt::CaseInsensitive) == 0 &&
                readsAsDead(h.value) && l.until < 0)
                l.until = col(h.chapter);
        }
        laneOf.insert(e.id, int(m_canvas->lanes.size()));
        m_canvas->lanes << l;
    }

    m_canvas->markers.clear();
    if (markerType && markerType != laneType) {
        const auto mt = m_world->type(markerType);
        for (const auto &ev : m_world->entities(markerType)) {
            QSet<Id> touched;
            for (const auto &l : m_world->allLinks(std::nullopt)) {
                if (l.src == ev.id && laneOf.contains(l.dst)) touched.insert(l.dst);
                if (l.dst == ev.id && laneOf.contains(l.src)) touched.insert(l.src);
            }
            for (Id who : touched)
                m_canvas->markers.append({ev.id, ev.name, typeColor(mt ? mt->color : QString()), col(ev.intro), laneOf[who]});
        }
    }
    m_canvas->relayout();
}

} // namespace worldsea

#include "moc_lanesview.cpp"
