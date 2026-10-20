#include "app/quickopen.h"

#include "app/theme.h"
#include "core/search.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace worldsea {

namespace {
enum Action { OpenItem, GoChapter, CreateItem };
constexpr int kActionRole = Qt::UserRole;
constexpr int kIdRole = Qt::UserRole + 1;
} // namespace

QuickOpen::QuickOpen(World *world, QWidget *parent) : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint), m_world(world)
{
    setObjectName("room");
    setStyleSheet(QStringLiteral("QDialog { border: 1px solid %1; border-radius: 10px; }").arg(theme().line.name()));
    setMinimumWidth(520);
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(10, 10, 10, 10);
    m_input = new QLineEdit;
    m_input->setPlaceholderText("Find an item, go to a chapter, or create something");
    m_list = new QListWidget;
    m_list->setMinimumHeight(320);
    dropFocusFrame(m_list);
    col->addWidget(m_input);
    col->addWidget(m_list);
    m_input->installEventFilter(this);
    setFocusProxy(m_input);
    connect(m_input, &QLineEdit::textChanged, this, &QuickOpen::update);
    connect(m_input, &QLineEdit::returnPressed, this, &QuickOpen::accept);
    connect(m_list, &QListWidget::itemActivated, this, &QuickOpen::accept);
    update();
}

void QuickOpen::showEvent(QShowEvent *e)
{
    QDialog::showEvent(e);
    activateWindow();
    m_input->setFocus();
}

bool QuickOpen::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_input && e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(e)->key();
        if (key == Qt::Key_Down || key == Qt::Key_Up) {
            int row = m_list->currentRow() + (key == Qt::Key_Down ? 1 : -1);
            // skip the rows that are only labels
            while (row >= 0 && row < m_list->count() && !(m_list->item(row)->flags() & Qt::ItemIsSelectable))
                row += key == Qt::Key_Down ? 1 : -1;
            if (row >= 0 && row < m_list->count()) m_list->setCurrentRow(row);
            return true;
        }
    }
    return QDialog::eventFilter(o, e);
}

void QuickOpen::update()
{
    m_list->clear();
    const QString q = m_input->text().trimmed();
    const Theme &th = theme();
    auto add = [&](const QString &text, Action a, Id id, const QColor &color = {}) {
        auto *item = new QListWidgetItem(text);
        item->setData(kActionRole, a);
        item->setData(kIdRole, id);
        if (color.isValid()) item->setForeground(color);
        m_list->addItem(item);
    };
    QHash<Id, EntityType> types;
    for (const auto &t : m_world->types()) types.insert(t.id, t);

    for (const auto &h : searchEntities(*m_world, q, q.isEmpty() ? 30 : 12)) {
        const auto t = types.value(h.entity.type);
        QString text = h.entity.name;
        if (h.matched != h.entity.name) text += "  (" + h.matched + ")";
        text += "    " + t.name;
        add(text, OpenItem, h.entity.id);
    }
    if (!q.isEmpty()) {
        int chapters = 0;
        for (Id ch : m_world->timeline()) {
            const QString label = m_world->chapterLabel(ch);
            if (fuzzyScore(q, label) < 0 || chapters >= 5) continue;
            add("Go to " + label, GoChapter, ch, th.muted);
            ++chapters;
        }
        for (const auto &t : m_world->types()) add(QStringLiteral("Create %1 “%2”").arg(t.name, q), CreateItem, t.id, th.accent);
    }
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->flags() & Qt::ItemIsSelectable) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void QuickOpen::accept()
{
    auto *item = m_list->currentItem();
    if (!item) return;
    const Id id = item->data(kIdRole).toLongLong();
    switch (item->data(kActionRole).toInt()) {
    case OpenItem: emit openEntity(id, false); break;
    case GoChapter: m_world->setCursor(id); break;
    case CreateItem: emit openEntity(m_world->addEntity(id, m_input->text().trimmed()), false); break;
    }
    QDialog::accept();
}

} // namespace worldsea

#include "moc_quickopen.cpp"
