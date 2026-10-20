#pragma once
// The left pane: books and their chapters, then every type with its items.
// The cursor's chapter is gold; items that only appear later are dimmed and
// placeholders are marked.

#include "core/world.h"

#include <QSet>
#include <QWidget>

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace worldsea {

class NavPane : public QWidget {
    Q_OBJECT
public:
    explicit NavPane(World *world, QWidget *parent = nullptr);

    // a new item of the type, opened for naming
    void newEntity(Id type);
    void newBook();
    void newChapter(Id book);
    void newType();
    void focusFilter();

signals:
    void openEntity(Id id, bool focusName = false);
    void openType(Id id);
    void openTable(Id id);
    void openGraph();
    void openChecks();
    void openHierarchy();
    void openLanes();
    void openMatrix();
    void openMap();

private:
    enum Role { KindRole = Qt::UserRole, IdRole };
    enum NodeKind { BooksHeader, ItemsHeader, BookNode, ChapterNode, TypeNode, EntityNode, ViewsHeader, GraphNode, ChecksNode, HierarchyNode, LanesNode, MatrixNode, MapNode };

    void rebuild();
    // where a dragged book, chapter or type may land, and landing it
    bool canDrop(int dragKind, QTreeWidgetItem *target, int indicator) const;
    void dropped(int dragKind, Id dragId, int targetKind, Id targetId, int indicator);
    void contextMenu(const QPoint &p);
    void activated(QTreeWidgetItem *item);
    void renamed(QTreeWidgetItem *item);
    void startRename(NodeKind kind, Id id);
    QTreeWidgetItem *find(NodeKind kind, Id id) const;
    static QString key(QTreeWidgetItem *item);

    World *m_world;
    QLineEdit *m_filter;
    QTreeWidget *m_tree;
    QSet<QString> m_collapsed;
    bool m_building = false;
};

} // namespace worldsea
