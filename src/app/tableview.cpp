#include "app/tableview.h"

#include "app/fieldeditors.h"
#include "app/theme.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QTableWidget>
#include <QVBoxLayout>

namespace worldsea {

namespace {

constexpr int kEntityRole = Qt::UserRole;
constexpr int kFieldRole = Qt::UserRole + 1;

// sorts by a hidden key instead of the text: ladders by rank, numbers by value
class Cell : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    QVariant key;
    bool operator<(const QTableWidgetItem &other) const override
    {
        const auto *o = dynamic_cast<const Cell *>(&other);
        if (!o || !key.isValid() || !o->key.isValid()) return text().localeAwareCompare(other.text()) < 0;
        if (key.typeId() == QMetaType::Double) return key.toDouble() < o->key.toDouble();
        return key.toInt() < o->key.toInt();
    }
};

} // namespace

TableView::TableView(World *world, Id type, QWidget *parent) : QWidget(parent), m_world(world), m_type(type)
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto *bar = new QWidget;
    bar->setObjectName("timeline");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(12, 6, 12, 6);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Filter rows");
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(260);
    m_count = new QLabel;
    m_count->setObjectName("muted");
    auto *hint = new QLabel("Right-click the header to choose columns · double-click a name to open");
    hint->setObjectName("muted");
    row->addWidget(m_filter);
    row->addWidget(m_count);
    row->addStretch();
    row->addWidget(hint);
    col->addWidget(bar);

    m_table = new QTableWidget;
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setSortingEnabled(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->horizontalHeader()->setSectionsMovable(true);
    m_table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->sortByColumn(0, Qt::AscendingOrder);
    dropFocusFrame(m_table);
    col->addWidget(m_table, 1);

    // images and long text make poor columns; they start hidden
    for (const auto &f : world->fields(type))
        if (f.kind == Kind::Image || f.kind == Kind::LongText) m_hidden.insert(f.id);

    connect(m_filter, &QLineEdit::textChanged, this, &TableView::rebuild);
    connect(m_table->horizontalHeader(), &QHeaderView::customContextMenuRequested, this, &TableView::columnMenu);
    connect(m_table, &QTableWidget::cellChanged, this, &TableView::edited);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int r, int c) {
        if (c == 0)
            if (auto *it = m_table->item(r, 0)) emit openEntity(it->data(kEntityRole).toLongLong());
    });
    connect(world, &World::changed, this, &TableView::rebuild);
    connect(world, &World::cursorChanged, this, &TableView::rebuild);
    rebuild();
}

void TableView::rebuild()
{
    const auto t = m_world->type(m_type);
    if (!t) {
        emit gone();
        return;
    }
    emit titleChanged(t->name);
    m_building = true;
    const int sortCol = m_table->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder order = m_table->horizontalHeader()->sortIndicatorOrder();
    const int scroll = m_table->verticalScrollBar() ? m_table->verticalScrollBar()->value() : 0;
    m_table->setSortingEnabled(false);
    m_table->clear();

    m_columns.clear();
    for (const auto &f : m_world->fields(m_type, m_world->cursorBook()))
        if (!m_hidden.contains(f.id)) m_columns << f;
    QStringList headers{"Name"};
    for (const auto &f : m_columns) headers << f.name;
    m_table->setColumnCount(int(headers.size()));
    m_table->setHorizontalHeaderLabels(headers);

    const Id cursor = m_world->cursor();
    const QString filter = m_filter->text().trimmed();
    const Theme &th = theme();
    QList<Entity> rows;
    for (const auto &e : m_world->entities(m_type)) {
        if (!filter.isEmpty()) {
            bool hit = e.name.contains(filter, Qt::CaseInsensitive);
            for (const auto &a : e.aliases) hit = hit || a.contains(filter, Qt::CaseInsensitive);
            if (!hit) continue;
        }
        rows << e;
    }
    m_table->setRowCount(int(rows.size()));
    int shown = 0;
    for (int r = 0; r < rows.size(); ++r) {
        const Entity &e = rows[r];
        const bool present = m_world->existsAt(e, cursor);
        shown += present;
        auto *name = new Cell(e.name);
        name->setData(kEntityRole, e.id);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        name->setToolTip(present ? QString() : "First appears in " + m_world->chapterLabel(e.intro));
        if (!present) name->setForeground(th.faint);
        if (e.stub) {
            QFont f = name->font();
            f.setItalic(true);
            name->setFont(f);
        }
        m_table->setItem(r, 0, name);
        for (int c = 0; c < m_columns.size(); ++c) {
            const FieldDef &f = m_columns[c];
            auto *cell = new Cell;
            cell->setData(kEntityRole, e.id);
            cell->setData(kFieldRole, f.id);
            Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
            if (f.kind == Kind::Reference) {
                QStringList names;
                for (const auto &v : m_world->links(e.id, f.id))
                    if (const auto o = m_world->entity(v.other)) names << o->name;
                cell->setText(names.join(", "));
            } else {
                const Effective eff = m_world->effective(e.id, f.id);
                if (f.kind == Kind::Ladder) {
                    int rank = 0;
                    for (int i = 0; i < f.steps.size(); ++i)
                        if (QString::number(f.steps[i].id) == eff.value) rank = i + 1;
                    cell->setText(rank ? f.steps[rank - 1].name : QString());
                    cell->key = rank;
                } else if (f.kind == Kind::Chapter) {
                    cell->setText(eff.has ? displayValue(*m_world, f, eff.value) : QString());
                    cell->key = m_world->ordinal(eff.value.toLongLong());
                } else if (f.kind == Kind::Number) {
                    cell->setText(eff.value);
                    bool ok = false;
                    const double v = eff.value.toDouble(&ok);
                    cell->key = ok ? v : -1e300;
                    cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    flags |= Qt::ItemIsEditable;
                } else if (f.kind == Kind::Text) {
                    cell->setText(eff.value);
                    flags |= Qt::ItemIsEditable;
                } else {
                    cell->setText(displayValue(*m_world, f, eff.value));
                    if (!eff.has) cell->setText({});
                }
                if (eff.has && eff.since == cursor && cursor) cell->setForeground(th.accent);
                if (eff.has && eff.since) cell->setToolTip("Since " + m_world->chapterLabel(eff.since));
            }
            if (!present) cell->setForeground(th.faint);
            cell->setFlags(flags);
            m_table->setItem(r, c + 1, cell);
        }
    }
    m_count->setText(QStringLiteral("%1 in the story as of this chapter, %2 in all").arg(shown).arg(rows.size()));
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(sortCol < m_table->columnCount() ? sortCol : 0, order);
    m_table->resizeColumnsToContents();
    for (int c = 0; c < m_table->columnCount(); ++c)
        m_table->setColumnWidth(c, std::clamp(m_table->columnWidth(c) + 24, 90, 360));
    if (m_table->verticalScrollBar()) m_table->verticalScrollBar()->setValue(scroll);
    m_building = false;
}

void TableView::edited(int row, int col)
{
    if (m_building || col == 0) return;
    auto *cell = m_table->item(row, col);
    if (!cell) return;
    const Id entity = cell->data(kEntityRole).toLongLong();
    const auto f = m_world->field(cell->data(kFieldRole).toLongLong());
    if (!f) return;
    const QString text = cell->text().trimmed();
    if (f->kind == Kind::Number && !text.isEmpty()) {
        bool ok = false;
        text.toDouble(&ok);
        if (!ok) {
            // not a number: put back what was there
            QMetaObject::invokeMethod(this, &TableView::rebuild, Qt::QueuedConnection);
            return;
        }
    }
    if (text == m_world->effective(entity, f->id).value) return;
    // the rebuild this triggers must not run inside the table's own signal
    QMetaObject::invokeMethod(this, [this, entity, id = f->id, name = f->name, text] {
        World::Step s(*m_world, "Edit " + name);
        m_world->setValue(entity, id, text);
        if (const auto e = m_world->entity(entity); e && e->stub) m_world->setStub(entity, false);
    }, Qt::QueuedConnection);
}

void TableView::columnMenu(const QPoint &p)
{
    QMenu menu;
    for (const auto &f : m_world->fields(m_type, m_world->cursorBook())) {
        auto *a = menu.addAction(f.name);
        a->setCheckable(true);
        a->setChecked(!m_hidden.contains(f.id));
        connect(a, &QAction::toggled, this, [this, id = f.id](bool on) {
            if (on) m_hidden.remove(id);
            else m_hidden.insert(id);
            rebuild();
        });
    }
    menu.exec(m_table->horizontalHeader()->mapToGlobal(p));
}

} // namespace worldsea

#include "moc_tableview.cpp"
