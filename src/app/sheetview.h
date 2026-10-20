#pragma once
// An item's sheet as of the cursor: name, aliases, first appearance, then
// every field of its type (for the cursor's book), with where each value
// came from. Beside it, the item's history along the timeline.

#include "core/world.h"

#include <QWidget>

#include <functional>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QVBoxLayout;

namespace worldsea {

class SheetView : public QWidget {
    Q_OBJECT
public:
    SheetView(World *world, Id entity, QWidget *parent = nullptr);
    Id entityId() const { return m_entity; }
    void focusName();

signals:
    void openEntity(Id id);
    void openType(Id id);
    void titleChanged(const QString &title);
    void gone();

private:
    struct Row {
        FieldDef field;
        QLabel *since = nullptr;
        std::function<void()> refresh;
    };

    void refresh();
    void buildFields();
    QString structureKey() const;
    QWidget *editorFor(const FieldDef &f, Row &row);
    void setValue(const FieldDef &f, const QString &value);
    void refreshSince(Row &row);
    void refreshHistory();
    void refreshBacklinks();
    void historyMenu(const QPoint &p);

    World *m_world;
    Id m_entity;
    QString m_structure;
    QLabel *m_typeChip, *m_stubNote, *m_absentNote;
    QLineEdit *m_name, *m_aliases;
    QComboBox *m_intro;
    QGridLayout *m_grid;
    QWidget *m_fieldsHost;
    QVBoxLayout *m_backlinks;
    QWidget *m_backlinksHost;
    QList<Row> m_rows;
    QListWidget *m_history;
    QComboBox *m_historyField;
};

} // namespace worldsea
