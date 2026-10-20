#pragma once
// A link field as a grid, as of the cursor: its items down the side, what it
// can link to across, a mark where they are linked. Secrets by who knows
// them, characters by their sects. Clicking a cell links or unlinks the pair
// the way any edit lands (a correction, or a change in this chapter).

#include "core/world.h"

#include <QWidget>

class QComboBox;
class QTableWidget;

namespace worldsea {

class MatrixView : public QWidget {
    Q_OBJECT
public:
    explicit MatrixView(World *world, QWidget *parent = nullptr);

signals:
    void openEntity(Id id);

private:
    void fillFields();
    void rebuild();
    void toggle(int row, int col);

    World *m_world;
    QComboBox *m_field;
    QTableWidget *m_table;
    QList<Id> m_rows, m_cols;
};

} // namespace worldsea
