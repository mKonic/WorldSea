#pragma once
// Items nested by a link field as of the cursor: places within places by
// "Part of", disciples under their master by "Master", members under their
// sect by "Affiliation". Whatever the field points at is the parent.

#include "core/world.h"

#include <QWidget>

class QComboBox;
class QTreeWidget;

namespace worldsea {

class HierarchyView : public QWidget {
    Q_OBJECT
public:
    explicit HierarchyView(World *world, QWidget *parent = nullptr);

signals:
    void openEntity(Id id);

private:
    void fillFields();
    void rebuild();

    World *m_world;
    QComboBox *m_field;
    QTreeWidget *m_tree;
};

} // namespace worldsea
