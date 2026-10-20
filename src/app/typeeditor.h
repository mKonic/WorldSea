#pragma once
// A type's sheet template: its name and color, its fields in order, and the
// selected field's kind, book, ladder steps, or link targets and other side.

#include "core/world.h"

#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace worldsea {

class TypeEditor : public QWidget {
    Q_OBJECT
public:
    TypeEditor(World *world, Id type, QWidget *parent = nullptr);
    Id typeId() const { return m_type; }

signals:
    void titleChanged(const QString &title);
    void gone();

private:
    void refresh();
    void refreshField();
    Id currentField() const;
    void saveField(const std::function<void(FieldDef &)> &change);

    World *m_world;
    Id m_type;
    QLineEdit *m_name;
    QPushButton *m_color;
    QListWidget *m_fields;
    QPushButton *m_up, *m_down, *m_delete;

    QWidget *m_detail;
    QLineEdit *m_fieldName;
    QComboBox *m_kind, *m_book;
    QLabel *m_kindHint;
    QStackedWidget *m_options;
    QListWidget *m_steps;
    QListWidget *m_targets;
    QCheckBox *m_multi;
    QLineEdit *m_inverse;
    QLabel *m_inverseHint;
    bool m_loading = false;
};

} // namespace worldsea
