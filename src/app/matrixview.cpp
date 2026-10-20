#include "app/matrixview.h"

#include "app/theme.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace worldsea {

MatrixView::MatrixView(World *world, QWidget *parent) : QWidget(parent), m_world(world)
{
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *bar = new QWidget;
    bar->setObjectName("timeline");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(14, 8, 14, 8);
    auto *label = new QLabel("Field");
    label->setObjectName("muted");
    m_field = new QComboBox;
    m_field->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    auto *hint = new QLabel("Click a cell to link or unlink · double-click a name to open");
    hint->setObjectName("muted");
    row->addWidget(label);
    row->addWidget(m_field);
    row->addStretch();
    row->addWidget(hint);
    col->addWidget(bar);

    m_table = new QTableWidget;
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->horizontalHeader()->setDefaultSectionSize(110);
    m_table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    m_table->verticalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->setShowGrid(true);
    dropFocusFrame(m_table);
    col->addWidget(m_table, 1);

    connect(m_field, &QComboBox::activated, this, &MatrixView::rebuild);
    connect(m_table, &QTableWidget::cellClicked, this, &MatrixView::toggle);
    connect(m_table->verticalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int r) {
        if (r < m_rows.size()) emit openEntity(m_rows[r]);
    });
    connect(m_table->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int c) {
        if (c < m_cols.size()) emit openEntity(m_cols[c]);
    });
    connect(world, &World::changed, this, [this] {
        fillFields();
        rebuild();
    });
    connect(world, &World::cursorChanged, this, &MatrixView::rebuild);
    connect(world, &World::editModeChanged, this, &MatrixView::rebuild);
    fillFields();
    rebuild();
}

void MatrixView::fillFields()
{
    const Id keep = m_field->currentData().toLongLong();
    QSignalBlocker b(m_field);
    m_field->clear();
    Id knownBy = 0;
    for (const auto &t : m_world->types()) {
        for (const auto &f : m_world->fields(t.id)) {
            if (f.kind != Kind::Reference || !f.multi) continue;
            m_field->addItem(t.name + " · " + f.name, f.id);
            if (!knownBy && f.name.compare("Known by", Qt::CaseInsensitive) == 0) knownBy = f.id;
        }
    }
    m_field->setCurrentIndex(std::max(0, m_field->findData(keep ? keep : knownBy)));
}

void MatrixView::rebuild()
{
    const auto f = m_world->field(m_field->currentData().toLongLong());
    m_table->clear();
    m_rows.clear();
    m_cols.clear();
    if (!f) {
        m_table->setRowCount(0);
        m_table->setColumnCount(0);
        return;
    }
    const Id cursor = m_world->cursor();
    const Theme &th = theme();
    QList<Id> targets = f->targets;
    if (targets.isEmpty())
        for (const auto &t : m_world->types()) targets << t.id;
    QStringList rowNames, colNames;
    for (const auto &e : m_world->entities(f->type)) {
        if (!m_world->existsAt(e, cursor)) continue;
        m_rows << e.id;
        rowNames << e.name;
    }
    for (const auto &e : m_world->entities()) {
        if (!targets.contains(e.type) || !m_world->existsAt(e, cursor)) continue;
        m_cols << e.id;
        colNames << e.name;
    }
    m_table->setRowCount(int(m_rows.size()));
    m_table->setColumnCount(int(m_cols.size()));
    m_table->setVerticalHeaderLabels(rowNames);
    m_table->setHorizontalHeaderLabels(colNames);
    const int at = m_world->ordinal(cursor);
    for (int r = 0; r < m_rows.size(); ++r) {
        QHash<Id, Link> linked;
        for (const auto &v : m_world->links(m_rows[r], f->id)) linked.insert(v.other, v.link);
        for (int c = 0; c < m_cols.size(); ++c) {
            auto *cell = new QTableWidgetItem;
            cell->setTextAlignment(Qt::AlignCenter);
            if (linked.contains(m_cols[c])) {
                const Link &l = linked[m_cols[c]];
                const bool here = l.start && m_world->ordinal(l.start) == at;
                cell->setText("●");
                cell->setForeground(here ? th.accent : th.text);
                cell->setToolTip(l.start ? "Since " + m_world->chapterLabel(l.start) : QStringLiteral("From the start"));
            } else {
                cell->setToolTip(m_world->editMode() == EditMode::Change && cursor ? "Click: linked from this chapter on"
                                                                                    : "Click: linked from the start");
            }
            m_table->setItem(r, c, cell);
        }
    }
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->resizeColumnsToContents();
    for (int c = 0; c < m_table->columnCount(); ++c)
        m_table->setColumnWidth(c, std::max(90, m_table->columnWidth(c) + 24));
}

void MatrixView::toggle(int row, int col)
{
    if (row >= m_rows.size() || col >= m_cols.size()) return;
    const Id fid = m_field->currentData().toLongLong();
    const Id a = m_rows[row], b = m_cols[col];
    // a rebuild is coming: leave the table's click handler first
    QMetaObject::invokeMethod(this, [this, fid, a, b] {
        for (const auto &v : m_world->links(a, fid)) {
            if (v.other == b) {
                m_world->removeLink(v.link.id);
                return;
            }
        }
        m_world->addLink(fid, a, b);
    }, Qt::QueuedConnection);
}

} // namespace worldsea

#include "moc_matrixview.cpp"
