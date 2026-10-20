#pragma once
// Ctrl+K: find any item by name or alias, jump to a chapter, or create an
// item of any type with the name typed.

#include "core/world.h"

#include <QDialog>

class QLineEdit;
class QListWidget;

namespace worldsea {

class QuickOpen : public QDialog {
    Q_OBJECT
public:
    QuickOpen(World *world, QWidget *parent = nullptr);

signals:
    void openEntity(Id id, bool focusName);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    void update();
    void accept() override;

    World *m_world;
    QLineEdit *m_input;
    QListWidget *m_list;
};

} // namespace worldsea
