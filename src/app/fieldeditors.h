#pragma once
// The editors a sheet is made of, one per field kind that needs more than a
// stock widget: long text that grows with its words, tags as chips, and
// reference fields as chips of linked items with a name box that finds or
// creates the item to link.

#include "core/world.h"

#include <QTextEdit>
#include <QWidget>

class QLineEdit;
class QStringListModel;

namespace worldsea {

class FlowLayout;

// a rounded label in a color, clickable, with an optional × to remove it
class Chip : public QWidget {
    Q_OBJECT
public:
    Chip(const QString &text, const QColor &color, bool removable, QWidget *parent = nullptr);
    void setDim(bool dim);

signals:
    void clicked();
    void removeClicked();
    void menuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    QColor m_color;
    bool m_dim = false;
};

// a plain-text box as tall as its text; committed when it loses focus
class GrowingTextEdit : public QTextEdit {
    Q_OBJECT
public:
    explicit GrowingTextEdit(QWidget *parent = nullptr);
    void setTextQuietly(const QString &text);

signals:
    void committed(const QString &text);

protected:
    void focusOutEvent(QFocusEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void fit();
    QString m_last;
};

class TagsEditor : public QWidget {
    Q_OBJECT
public:
    explicit TagsEditor(QWidget *parent = nullptr);
    void setTags(const QStringList &tags);
    QStringList tags() const { return m_tags; }

signals:
    void edited(const QStringList &tags);

private:
    void rebuild();
    QStringList m_tags;
    FlowLayout *m_flow;
    QLineEdit *m_input;
};

// a field's links as of the cursor
class RefEditor : public QWidget {
    Q_OBJECT
public:
    RefEditor(World *world, Id entity, const FieldDef &field, QWidget *parent = nullptr);
    void refresh();

signals:
    void openEntity(Id id);

private:
    void submit();
    void linkTo(Id other);
    void chipMenu(const LinkView &v, const QPoint &globalPos);
    QList<Id> targetTypes() const;

    World *m_world;
    Id m_entity;
    FieldDef m_field;
    FlowLayout *m_flow;
    QLineEdit *m_input;
    QStringListModel *m_names;
};

} // namespace worldsea
