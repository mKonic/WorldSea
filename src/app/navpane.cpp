#include "app/navpane.h"

#include "app/theme.h"
#include "core/continuity.h"

#include <QDropEvent>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace worldsea {

namespace {

QIcon dot(const QColor &c, bool hollow = false)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    if (hollow) {
        p.setPen(QPen(c, 1.4));
        p.setBrush(Qt::NoBrush);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
    }
    p.drawEllipse(QRectF(2.5, 2.5, 7, 7));
    return QIcon(pm);
}

// QAbstractItemView::DropIndicatorPosition, which is protected
constexpr int kOn = 0, kAbove = 1, kBelow = 2;

// a tree whose drags are decided by the pane, and land through the world
// rather than by Qt moving rows around
class NavTree : public QTreeWidget {
public:
    std::function<bool(QTreeWidgetItem *dragged, QTreeWidgetItem *target, int indicator)> allow;
    std::function<void(QTreeWidgetItem *dragged, QTreeWidgetItem *target, int indicator)> land;

protected:
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        QTreeWidget::dragMoveEvent(e);
        QTreeWidgetItem *target = itemAt(e->position().toPoint());
        if (!currentItem() || !target || !allow(currentItem(), target, dropIndicatorPosition())) e->ignore();
    }
    void dropEvent(QDropEvent *e) override
    {
        QTreeWidgetItem *target = itemAt(e->position().toPoint());
        QTreeWidgetItem *dragged = currentItem();
        const int indicator = dropIndicatorPosition();
        // Qt must not move the row itself: the world does, then the tree is rebuilt
        e->setDropAction(Qt::IgnoreAction);
        e->accept();
        if (dragged && target && allow(dragged, target, indicator)) land(dragged, target, indicator);
    }
};

} // namespace

NavPane::NavPane(World *world, QWidget *parent) : QWidget(parent), m_world(world)
{
    setObjectName("room");
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(8, 8, 0, 8);
    col->setSpacing(6);

    auto *top = new QHBoxLayout;
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText("Filter items");
    m_filter->setClearButtonEnabled(true);
    auto *add = new QToolButton;
    add->setText("+");
    add->setObjectName("square");
    add->setFixedSize(32, 32);
    add->setToolTip("New item, type, book or chapter");
    add->setPopupMode(QToolButton::InstantPopup);
    auto *addMenu = new QMenu(add);
    connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu] {
        addMenu->clear();
        for (const auto &t : m_world->types())
            addMenu->addAction(dot(typeColor(t.color)), "New " + t.name, this, [this, id = t.id] { newEntity(id); });
        addMenu->addSeparator();
        addMenu->addAction("New type", this, &NavPane::newType);
        addMenu->addAction("New book", this, &NavPane::newBook);
        if (const Id b = m_world->cursorBook()) addMenu->addAction("New chapter", this, [this, b] { newChapter(b); });
    });
    add->setMenu(addMenu);
    top->addWidget(m_filter, 1);
    top->addWidget(add);
    col->addLayout(top);

    auto *tree = new NavTree;
    m_tree = tree;
    tree->setDragDropMode(QAbstractItemView::InternalMove);
    tree->setDropIndicatorShown(true);
    tree->allow = [this](QTreeWidgetItem *d, QTreeWidgetItem *t, int indicator) {
        return canDrop(d->data(0, KindRole).toInt(), t, indicator);
    };
    tree->land = [this](QTreeWidgetItem *d, QTreeWidgetItem *t, int indicator) {
        // the rebuild that follows deletes these items: pass ids, and wait for the drop to finish
        QMetaObject::invokeMethod(this, [this, dk = d->data(0, KindRole).toInt(), did = d->data(0, IdRole).toLongLong(),
                                         tk = t->data(0, KindRole).toInt(), tid = t->data(0, IdRole).toLongLong(), indicator] {
            dropped(dk, did, tk, tid, indicator);
        }, Qt::QueuedConnection);
    };
    m_tree->setHeaderHidden(true);
    m_tree->setIndentation(14);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    m_tree->setExpandsOnDoubleClick(false);
    dropFocusFrame(m_tree);
    col->addWidget(m_tree, 1);

    connect(m_filter, &QLineEdit::textChanged, this, &NavPane::rebuild);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, &NavPane::contextMenu);
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *i) { activated(i); });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *i) { activated(i); });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *i) {
        switch (i->data(0, KindRole).toInt()) {
        case EntityNode: emit openEntity(i->data(0, IdRole).toLongLong()); break;
        case GraphNode: emit openGraph(); break;
        case ChecksNode: emit openChecks(); break;
        case HierarchyNode: emit openHierarchy(); break;
        case LanesNode: emit openLanes(); break;
        case MatrixNode: emit openMatrix(); break;
        case MapNode: emit openMap(); break;
        }
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *i) { renamed(i); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *i) {
        if (!m_building) m_collapsed.insert(key(i));
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *i) {
        if (!m_building) m_collapsed.remove(key(i));
    });
    connect(world, &World::changed, this, &NavPane::rebuild);
    connect(world, &World::cursorChanged, this, &NavPane::rebuild);
    rebuild();
}

QString NavPane::key(QTreeWidgetItem *item)
{
    return QStringLiteral("%1:%2").arg(item->data(0, KindRole).toInt()).arg(item->data(0, IdRole).toLongLong());
}

bool NavPane::canDrop(int dragKind, QTreeWidgetItem *target, int indicator) const
{
    const int tk = target->data(0, KindRole).toInt();
    const bool between = indicator == kAbove || indicator == kBelow;
    switch (dragKind) {
    case ChapterNode: return tk == ChapterNode || (tk == BookNode && indicator == kOn);
    case BookNode: return tk == BookNode && between;
    case TypeNode: return tk == TypeNode && between;
    default: return false;
    }
}

void NavPane::dropped(int dragKind, Id dragId, int targetKind, Id targetId, int indicator)
{
    const bool below = indicator != kAbove;
    // the index the dragged one takes in a list it may already be in
    auto landing = [&](const QList<Id> &order) {
        int i = int(order.indexOf(targetId)) + (below ? 1 : 0);
        const int from = int(order.indexOf(dragId));
        if (from >= 0 && from < i) --i;
        return i;
    };
    if (dragKind == ChapterNode) {
        if (targetKind == BookNode) {
            m_world->moveChapter(dragId, targetId, -1);
            return;
        }
        const auto t = m_world->chapter(targetId);
        if (!t || targetId == dragId) return;
        QList<Id> order;
        for (const auto &c : m_world->chapters(t->book)) order << c.id;
        m_world->moveChapter(dragId, t->book, landing(order));
    } else if (dragKind == BookNode) {
        QList<Id> order;
        for (const auto &b : m_world->books()) order << b.id;
        if (targetId != dragId) m_world->moveBook(dragId, landing(order));
    } else if (dragKind == TypeNode) {
        QList<Id> order;
        for (const auto &t : m_world->types()) order << t.id;
        if (targetId != dragId) m_world->moveType(dragId, landing(order));
    }
}

void NavPane::focusFilter()
{
    m_filter->setFocus();
    m_filter->selectAll();
}

void NavPane::rebuild()
{
    if (!m_world->isOpen()) {
        m_tree->clear();
        return;
    }
    m_building = true;
    QString selected;
    if (auto *cur = m_tree->currentItem()) selected = key(cur);
    const int scroll = m_tree->verticalScrollBar() ? m_tree->verticalScrollBar()->value() : 0;
    m_tree->clear();
    const QString filter = m_filter->text().trimmed();
    const Theme &th = theme();

    auto header = [&](const QString &text, NodeKind kind) {
        auto *h = new QTreeWidgetItem(m_tree, {text});
        h->setData(0, KindRole, kind);
        h->setData(0, IdRole, 0);
        QFont f = h->font(0);
        f.setPixelSize(11);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        h->setFont(0, f);
        h->setForeground(0, th.muted);
        h->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);
        return h;
    };

    if (filter.isEmpty()) {
        auto *views = header("VIEWS", ViewsHeader);
        for (auto [text, kind, tip] : {std::tuple{"Graph", GraphNode, "Every item and link as of this chapter (Ctrl+G)"},
                                       std::tuple{"Timeline", LanesNode, "Characters in lanes across the chapters (Ctrl+L)"},
                                       std::tuple{"Map", MapNode, "Places pinned on an image, and who is there now (Ctrl+Shift+P)"},
                                       std::tuple{"Who knows what", MatrixNode, "A link field as a grid: secrets by who knows them (Ctrl+Shift+M)"},
                                       std::tuple{"Hierarchy", HierarchyNode, "Items nested by a link field (Ctrl+Shift+H)"},
                                       std::tuple{"Continuity checks", ChecksNode, "What contradicts itself (Ctrl+Shift+C)"}}) {
            auto *v = new QTreeWidgetItem(views, {text});
            v->setData(0, KindRole, kind);
            v->setData(0, IdRole, 0);
            v->setToolTip(0, tip);
            v->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            if (kind == ChecksNode) {
                int warnings = 0;
                for (const auto &i : checkContinuity(*m_world)) warnings += i.severity == Issue::Warning;
                if (warnings) {
                    v->setText(0, QStringLiteral("Continuity checks  %1").arg(warnings));
                    v->setIcon(0, dot(QColor("#d9822b")));
                }
            }
        }
        auto *booksHeader = header("BOOKS", BooksHeader);
        for (const auto &b : m_world->books()) {
            auto *bi = new QTreeWidgetItem(booksHeader, {b.name});
            bi->setData(0, KindRole, BookNode);
            bi->setData(0, IdRole, b.id);
            bi->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            for (const auto &c : m_world->chapters(b.id)) {
                auto *ci = new QTreeWidgetItem(bi, {c.title});
                ci->setData(0, KindRole, ChapterNode);
                ci->setData(0, IdRole, c.id);
                ci->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
                if (c.id == m_world->cursor()) {
                    ci->setForeground(0, th.accent);
                    ci->setIcon(0, dot(th.accent));
                    ci->setToolTip(0, "The chapter the world is shown as of");
                } else {
                    ci->setIcon(0, dot(th.line, true));
                    ci->setToolTip(0, "Double-click to view the world as of this chapter");
                }
            }
        }
    }

    auto *itemsHeader = header("ITEMS", ItemsHeader);
    const Id cursor = m_world->cursor();
    for (const auto &t : m_world->types()) {
        const auto entities = m_world->entities(t.id);
        auto *ti = new QTreeWidgetItem(itemsHeader, {t.name});
        ti->setData(0, KindRole, TypeNode);
        ti->setData(0, IdRole, t.id);
        ti->setIcon(0, dot(typeColor(t.color)));
        ti->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        int shown = 0;
        for (const auto &e : entities) {
            if (!filter.isEmpty()) {
                bool hit = e.name.contains(filter, Qt::CaseInsensitive);
                for (const auto &a : e.aliases) hit = hit || a.contains(filter, Qt::CaseInsensitive);
                if (!hit) continue;
            }
            ++shown;
            auto *ei = new QTreeWidgetItem(ti, {e.name});
            ei->setData(0, KindRole, EntityNode);
            ei->setData(0, IdRole, e.id);
            ei->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            QStringList tip;
            if (!e.aliases.isEmpty()) tip << "Also: " + e.aliases.join(", ");
            if (!m_world->existsAt(e, cursor)) {
                ei->setForeground(0, th.faint);
                tip << "First appears in " + m_world->chapterLabel(e.intro);
            }
            if (e.stub) {
                QFont f = ei->font(0);
                f.setItalic(true);
                ei->setFont(0, f);
                tip << "Placeholder: nothing filled in yet";
            }
            ei->setToolTip(0, tip.join('\n'));
        }
        ti->setText(0, QStringLiteral("%1  %2").arg(t.name).arg(entities.size()));
        if (!filter.isEmpty() && !shown) delete ti;
    }

    // expand everything not collapsed by hand; a filter shows every hit
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if ((*it)->childCount()) (*it)->setExpanded(!filter.isEmpty() || !m_collapsed.contains(key(*it)));
        if (key(*it) == selected) m_tree->setCurrentItem(*it);
    }
    if (m_tree->verticalScrollBar()) m_tree->verticalScrollBar()->setValue(scroll);
    m_building = false;
}

QTreeWidgetItem *NavPane::find(NodeKind kind, Id id) const
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it)
        if ((*it)->data(0, KindRole).toInt() == kind && (*it)->data(0, IdRole).toLongLong() == id) return *it;
    return nullptr;
}

void NavPane::startRename(NodeKind kind, Id id)
{
    if (auto *i = find(kind, id)) {
        m_building = true;
        // types show their count; edit the bare name
        if (kind == TypeNode) i->setText(0, m_world->type(id)->name);
        m_building = false;
        m_tree->scrollToItem(i);
        m_tree->setCurrentItem(i);
        m_tree->editItem(i);
    }
}

void NavPane::renamed(QTreeWidgetItem *item)
{
    if (m_building) return;
    const Id id = item->data(0, IdRole).toLongLong();
    const QString text = item->text(0).trimmed();
    // a rebuild from inside itemChanged would delete the item under Qt's feet
    QMetaObject::invokeMethod(this, [this, kind = item->data(0, KindRole).toInt(), id, text] {
        if (text.isEmpty()) {
            rebuild();
            return;
        }
        switch (kind) {
        case BookNode: m_world->renameBook(id, text); break;
        case ChapterNode: m_world->renameChapter(id, text); break;
        case TypeNode: m_world->renameType(id, text); break;
        case EntityNode: m_world->renameEntity(id, text); break;
        }
        rebuild();
    }, Qt::QueuedConnection);
}

void NavPane::activated(QTreeWidgetItem *item)
{
    const Id id = item->data(0, IdRole).toLongLong();
    switch (item->data(0, KindRole).toInt()) {
    case ChapterNode: m_world->setCursor(id); break;
    case EntityNode: emit openEntity(id); break;
    case TypeNode: emit openType(id); break;
    case GraphNode: emit openGraph(); break;
    case ChecksNode: emit openChecks(); break;
    case HierarchyNode: emit openHierarchy(); break;
    case LanesNode: emit openLanes(); break;
    case MatrixNode: emit openMatrix(); break;
    case MapNode: emit openMap(); break;
    default: item->setExpanded(!item->isExpanded());
    }
}

void NavPane::newEntity(Id type)
{
    const auto t = m_world->type(type);
    if (!t) return;
    const Id id = m_world->addEntity(type, "New " + t->name);
    emit openEntity(id, true);
}

void NavPane::newBook()
{
    const int n = int(m_world->books().size()) + 1;
    const Id b = m_world->addBook(QStringLiteral("Book %1").arg(n));
    m_world->addChapter(b, "Chapter 1");
    startRename(BookNode, b);
}

void NavPane::newChapter(Id book)
{
    const int n = int(m_world->chapters(book).size()) + 1;
    const Id c = m_world->addChapter(book, QStringLiteral("Chapter %1").arg(n));
    startRename(ChapterNode, c);
}

void NavPane::newType()
{
    const Id t = m_world->addType("New type");
    emit openType(t);
}

void NavPane::contextMenu(const QPoint &p)
{
    QTreeWidgetItem *item = m_tree->itemAt(p);
    if (!item) return;
    const Id id = item->data(0, IdRole).toLongLong();
    const auto kind = NodeKind(item->data(0, KindRole).toInt());
    QMenu menu;
    auto moveActions = [&](auto move, int pos, int count) {
        menu.addAction("Move up", this, [=] { move(pos - 1); })->setEnabled(pos > 0);
        menu.addAction("Move down", this, [=] { move(pos + 1); })->setEnabled(pos + 1 < count);
    };
    switch (kind) {
    case ViewsHeader:
    case GraphNode:
    case ChecksNode:
    case HierarchyNode:
    case LanesNode:
    case MatrixNode:
    case MapNode:
        return;
    case BooksHeader:
        menu.addAction("New book", this, &NavPane::newBook);
        break;
    case ItemsHeader:
        menu.addAction("New type", this, &NavPane::newType);
        break;
    case BookNode: {
        const auto books = m_world->books();
        int pos = 0;
        for (int i = 0; i < books.size(); ++i)
            if (books[i].id == id) pos = i;
        menu.addAction("New chapter", this, [=, this] { newChapter(id); });
        menu.addAction("Rename", this, [=, this] { startRename(BookNode, id); });
        moveActions([=, this](int to) { m_world->moveBook(id, to); }, pos, int(books.size()));
        menu.addSeparator();
        menu.addAction("Delete book", this, [=, this] {
            int recorded = 0;
            for (const auto &c : m_world->chapters(id)) recorded += m_world->recordedIn(c.id);
            QString text = QStringLiteral("Delete “%1” and its chapters?").arg(m_world->book(id)->name);
            if (recorded) text += QStringLiteral("\n\n%1 changes recorded in them move to the chapter before.").arg(recorded);
            if (QMessageBox::question(this, "Delete book", text) == QMessageBox::Yes) m_world->deleteBook(id);
        });
        break;
    }
    case ChapterNode: {
        const auto c = m_world->chapter(id);
        const auto siblings = m_world->chapters(c->book);
        int pos = 0;
        for (int i = 0; i < siblings.size(); ++i)
            if (siblings[i].id == id) pos = i;
        menu.addAction("View as of this chapter", this, [=, this] { m_world->setCursor(id); });
        menu.addAction("Insert chapter after", this, [=, this] {
            const Id n = m_world->addChapter(c->book, "New chapter", pos + 1);
            startRename(ChapterNode, n);
        });
        menu.addAction("Rename", this, [=, this] { startRename(ChapterNode, id); });
        moveActions([=, this](int to) { m_world->moveChapter(id, c->book, to); }, pos, int(siblings.size()));
        const auto books = m_world->books();
        if (books.size() > 1) {
            auto *to = menu.addMenu("Move to book");
            for (const auto &b : books)
                if (b.id != c->book) to->addAction(b.name, this, [=, this] { m_world->moveChapter(id, b.id, -1); });
        }
        menu.addSeparator();
        menu.addAction("Delete chapter", this, [=, this] {
            QString text = QStringLiteral("Delete “%1”?").arg(c->title);
            if (const int n = m_world->recordedIn(id))
                text += QStringLiteral("\n\n%1 changes recorded in it move to the chapter before.").arg(n);
            if (QMessageBox::question(this, "Delete chapter", text) == QMessageBox::Yes) m_world->deleteChapter(id);
        });
        break;
    }
    case TypeNode: {
        const auto t = m_world->type(id);
        const auto types = m_world->types();
        int pos = 0;
        for (int i = 0; i < types.size(); ++i)
            if (types[i].id == id) pos = i;
        menu.addAction("New " + t->name, this, [=, this] { newEntity(id); });
        menu.addAction("Open as table", this, [=, this] { emit openTable(id); });
        menu.addAction("Edit sheet", this, [=, this] { emit openType(id); });
        menu.addAction("Rename", this, [=, this] { startRename(TypeNode, id); });
        moveActions([=, this](int to) { m_world->moveType(id, to); }, pos, int(types.size()));
        menu.addSeparator();
        menu.addAction("Delete type", this, [=, this] {
            const int n = int(m_world->entities(id).size());
            QString text = QStringLiteral("Delete the type “%1”?").arg(t->name);
            if (n) text += QStringLiteral("\n\nIts %1 items go with it.").arg(n);
            if (QMessageBox::question(this, "Delete type", text) == QMessageBox::Yes) m_world->deleteType(id);
        });
        break;
    }
    case EntityNode: {
        menu.addAction("Open", this, [=, this] { emit openEntity(id); });
        menu.addAction("Rename", this, [=, this] { startRename(EntityNode, id); });
        menu.addSeparator();
        menu.addAction("Delete", this, [=, this] {
            const auto e = m_world->entity(id);
            if (QMessageBox::question(this, "Delete item", QStringLiteral("Delete “%1” and its links?").arg(e->name)) == QMessageBox::Yes)
                m_world->deleteEntity(id);
        });
        break;
    }
    }
    menu.exec(m_tree->viewport()->mapToGlobal(p));
}

} // namespace worldsea

#include "moc_navpane.cpp"
