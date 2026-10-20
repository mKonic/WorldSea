#pragma once
// The continuity checks as a list: what to fix first, then what is worth a
// look. Double-click one to open the item as of the chapter it is about.

#include "core/world.h"

#include <QWidget>

class QLabel;
class QListWidget;

namespace worldsea {

class ChecksView : public QWidget {
    Q_OBJECT
public:
    explicit ChecksView(World *world, QWidget *parent = nullptr);
    void rebuild();

signals:
    void openEntity(Id id);
    void titleChanged(const QString &title);

private:
    World *m_world;
    QLabel *m_summary;
    QListWidget *m_list;
};

} // namespace worldsea
