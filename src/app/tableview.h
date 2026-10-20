#pragma once
// Every item of a type in rows, its fields in columns, as of the cursor:
// "where is everyone's cultivation right now". Sortable (ladders by rank),
// columns chosen from the header's menu, text and numbers edited in place,
// a row double-clicked opens the item's sheet.

#include "core/world.h"

#include <QSet>
#include <QWidget>

class QLabel;
class QLineEdit;
class QTableWidget;

namespace worldsea {

class TableView : public QWidget {
    Q_OBJECT
public:
    TableView(World *world, Id type, QWidget *parent = nullptr);
    Id typeId() const { return m_type; }

signals:
    void openEntity(Id id);
    void titleChanged(const QString &title);
    void gone();

private:
    void rebuild();
    void columnMenu(const QPoint &p);
    void edited(int row, int col);

    World *m_world;
    Id m_type;
    QTableWidget *m_table;
    QLineEdit *m_filter;
    QLabel *m_count;
    QSet<Id> m_hidden; // fields left out of the columns
    QList<FieldDef> m_columns;
    bool m_building = false;
};

} // namespace worldsea
