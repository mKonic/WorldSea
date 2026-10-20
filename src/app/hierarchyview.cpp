#include "app/hierarchyview.h"

#include "app/theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace worldsea {

namespace {

QIcon dot(const QColor &c)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawEllipse(QRectF(2.5, 2.5, 7, 7));
    return QIcon(pm);
}

} // namespace

HierarchyView::HierarchyView(World *world, QWidget *parent) : QWidget(parent), m_world(world)
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *bar = new QWidget;
    bar->setObjectName("timeline");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(14, 8, 14, 8);
    auto *label = new QLabel("Nest by");
    label->setObjectName("muted");
    m_field = new QComboBox;
    m_field->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto *hint = new QLabel("Each item sits under what its field links to · double-click to open");
    hint->setObjectName("muted");
    row->addWidget(label);
    row->addWidget(m_field);
    row->addStretch();
    row->addWidget(hint);
    col->addWidget(bar);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(22);
    dropFocusFrame(m_tree);
    col->addWidget(m_tree, 1);

    connect(m_field, &QComboBox::activated, this, &HierarchyView::rebuild);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *i) {
        emit openEntity(i->data(0, Qt::UserRole).toLongLong());
    });
    connect(world, &World::changed, this, [this] {
        fillFields();
        rebuild();
    });
    connect(world, &World::cursorChanged, this, &HierarchyView::rebuild);
    fillFields();
    rebuild();
}

void HierarchyView::fillFields()
{
    const Id keep = m_field->currentData().toLongLong();
    QSignalBlocker b(m_field);
    m_field->clear();
    // the likeliest tree first: a single-valued field pointing at its own type
    Id best = 0;
    for (const auto &t : m_world->types()) {
        for (const auto &f : m_world->fields(t.id)) {
            if (f.kind != Kind::Reference) continue;
            m_field->addItem(t.name + " · " + f.name, f.id);
            if (!best && !f.multi && f.targets == QList<Id>{t.id}) best = f.id;
        }
    }
    const int i = m_field->findData(keep ? keep : best);
    m_field->setCurrentIndex(std::max(0, i));
}

void HierarchyView::rebuild()
{
    m_tree->clear();
    const auto f = m_world->field(m_field->currentData().toLongLong());
    if (!f) return;
    const Id cursor = m_world->cursor();
    QHash<Id, Entity> items;
    for (const auto &e : m_world->entities())
        if (m_world->existsAt(e, cursor)) items.insert(e.id, e);
    QHash<Id, QString> colors;
    for (const auto &t : m_world->types()) colors.insert(t.id, t.color);

    // child -> parents through the field, as of the cursor
    QHash<Id, QList<Id>> children;
    QSet<Id> hasParent;
    for (const auto &e : items) {
        if (e.type != f->type) continue;
        for (const auto &v : m_world->links(e.id, f->id)) {
            if (!items.contains(v.other)) continue;
            children[v.other] << e.id;
            hasParent.insert(e.id);
        }
    }
    QList<Id> roots;
    for (const auto &e : items) {
        if (hasParent.contains(e.id)) continue;
        // an item of another type only shows when something hangs under it
        if (e.type == f->type || children.contains(e.id)) roots << e.id;
    }
    auto byName = [&](QList<Id> &ids) {
        std::sort(ids.begin(), ids.end(), [&](Id a, Id b) { return items[a].name.localeAwareCompare(items[b].name) < 0; });
    };
    byName(roots);
    // roots with children first, loose items after
    std::stable_partition(roots.begin(), roots.end(), [&](Id id) { return children.contains(id); });

    QSet<Id> path; // guards against a loop (A part of B part of A)
    std::function<void(QTreeWidgetItem *, Id)> add = [&](QTreeWidgetItem *parent, Id id) {
        const Entity &e = items[id];
        auto *item = parent ? new QTreeWidgetItem(parent, {e.name}) : new QTreeWidgetItem(m_tree, {e.name});
        item->setData(0, Qt::UserRole, id);
        item->setIcon(0, dot(typeColor(colors.value(e.type))));
        if (path.contains(id)) {
            item->setText(0, e.name + "  (loops back)");
            item->setForeground(0, theme().faint);
            return;
        }
        path.insert(id);
        QList<Id> kids = children.value(id);
        byName(kids);
        for (Id k : kids) add(item, k);
        path.remove(id);
        if (!kids.isEmpty()) item->setText(0, QStringLiteral("%1   %2").arg(e.name).arg(kids.size()));
    };
    for (Id r : roots) add(nullptr, r);
    // a loop has no root at all: show it from any member
    QSet<Id> shown;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) shown.insert((*it)->data(0, Qt::UserRole).toLongLong());
    for (const auto &e : items)
        if (e.type == f->type && !shown.contains(e.id)) add(nullptr, e.id);
    m_tree->expandAll();
}

} // namespace worldsea

#include "moc_hierarchyview.cpp"
