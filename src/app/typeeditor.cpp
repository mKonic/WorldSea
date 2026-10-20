#include "app/typeeditor.h"

#include "app/theme.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace worldsea {

namespace {

// a field's name, and its kind in muted letters on the right
class FieldRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem o = option;
        initStyleOption(&o, index);
        const QString kind = index.data(Qt::UserRole + 1).toString();
        o.text.clear();
        o.widget->style()->drawControl(QStyle::CE_ItemViewItem, &o, p, o.widget);
        const QRect r = o.rect.adjusted(10, 0, -10, 0);
        p->setPen(theme().text);
        p->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, index.data(Qt::DisplayRole).toString());
        p->setPen(theme().muted);
        p->drawText(r, Qt::AlignVCenter | Qt::AlignRight, kind);
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return QStyledItemDelegate::sizeHint(option, index) + QSize(0, 10);
    }
};

QString kindHint(Kind k)
{
    switch (k) {
    case Kind::Text: return "One line of text.";
    case Kind::LongText: return "Paragraphs: descriptions, notes, backstory.";
    case Kind::Number: return "A number, like an age or a count.";
    case Kind::Ladder: return "One step of an ordered list, like realms or ranks.";
    case Kind::Tags: return "Any number of short labels.";
    case Kind::Image: return "A picture: a portrait, a map, a sigil.";
    case Kind::Reference: return "Links to other items. Each link is a relation and shows in the graph.";
    }
    return {};
}

} // namespace

TypeEditor::TypeEditor(World *world, Id type, QWidget *parent) : QWidget(parent), m_world(world), m_type(type)
{
    setObjectName("room");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 22, 28, 22);
    outer->setSpacing(12);

    auto *head = new QHBoxLayout;
    m_color = new QPushButton;
    m_color->setFixedSize(28, 28);
    m_color->setToolTip("Color");
    m_name = new QLineEdit;
    m_name->setObjectName("nameEdit");
    head->addWidget(m_color);
    head->addWidget(m_name, 1);
    outer->addLayout(head);
    auto *sub = new QLabel("The sheet every item of this type opens with. Changes apply to all of them.");
    sub->setObjectName("muted");
    outer->addWidget(sub);

    auto *body = new QHBoxLayout;
    body->setSpacing(20);
    outer->addLayout(body, 1);

    // the field list
    auto *left = new QVBoxLayout;
    auto *fieldsTitle = new QLabel("FIELDS");
    fieldsTitle->setObjectName("heading");
    m_fields = new QListWidget;
    m_fields->setMinimumWidth(240);
    m_fields->setMaximumWidth(320);
    m_fields->setItemDelegate(new FieldRowDelegate(m_fields));
    auto *add = new QPushButton("Add field");
    auto *addMenu = new QMenu(add);
    for (Kind k : allKinds()) {
        addMenu->addAction(kindLabel(k), this, [this, k] {
            const Id id = m_world->addField(m_type, "New field", k);
            refresh();
            for (int i = 0; i < m_fields->count(); ++i)
                if (m_fields->item(i)->data(Qt::UserRole).toLongLong() == id) m_fields->setCurrentRow(i);
            m_fieldName->setFocus();
            m_fieldName->selectAll();
        });
    }
    add->setMenu(addMenu);
    m_up = new QPushButton("↑");
    m_down = new QPushButton("↓");
    m_delete = new QPushButton("Delete");
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(add);
    buttons->addStretch();
    buttons->addWidget(m_up);
    buttons->addWidget(m_down);
    buttons->addWidget(m_delete);
    left->addWidget(fieldsTitle);
    left->addWidget(m_fields, 1);
    left->addLayout(buttons);
    body->addLayout(left);

    // the selected field
    m_detail = new QWidget;
    auto *form = new QFormLayout(m_detail);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(10);
    m_fieldName = new QLineEdit;
    m_kind = new QComboBox;
    for (Kind k : allKinds()) m_kind->addItem(kindLabel(k), kindKey(k));
    m_kindHint = new QLabel;
    m_kindHint->setObjectName("muted");
    m_kindHint->setWordWrap(true);
    m_book = new QComboBox;
    form->addRow("Name", m_fieldName);
    form->addRow("Kind", m_kind);
    form->addRow("", m_kindHint);
    form->addRow("Shown in", m_book);

    m_options = new QStackedWidget;
    m_options->addWidget(new QWidget); // 0: nothing to set

    // 1: ladder steps
    auto *ladder = new QWidget;
    auto *ladderCol = new QVBoxLayout(ladder);
    ladderCol->setContentsMargins(0, 0, 0, 0);
    auto *stepsTitle = new QLabel("Steps, lowest first. Double-click to rename.");
    stepsTitle->setObjectName("muted");
    m_steps = new QListWidget;
    m_steps->setMinimumHeight(160);
    m_steps->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    auto *stepButtons = new QHBoxLayout;
    auto *addStep = new QPushButton("Add step");
    auto *stepUp = new QPushButton("↑");
    auto *stepDown = new QPushButton("↓");
    auto *delStep = new QPushButton("Delete");
    stepButtons->addWidget(addStep);
    stepButtons->addStretch();
    stepButtons->addWidget(stepUp);
    stepButtons->addWidget(stepDown);
    stepButtons->addWidget(delStep);
    ladderCol->addWidget(stepsTitle);
    ladderCol->addWidget(m_steps);
    ladderCol->addLayout(stepButtons);
    m_options->addWidget(ladder);

    // 2: reference targets and the other side
    auto *ref = new QWidget;
    auto *refForm = new QFormLayout(ref);
    refForm->setContentsMargins(0, 0, 0, 0);
    refForm->setHorizontalSpacing(14);
    m_targets = new QListWidget;
    m_targets->setMaximumHeight(150);
    m_multi = new QCheckBox("More than one at a time");
    m_inverse = new QLineEdit;
    m_inverse->setPlaceholderText("e.g. Disciples for Master");
    m_inverseHint = new QLabel;
    m_inverseHint->setObjectName("muted");
    m_inverseHint->setWordWrap(true);
    refForm->addRow("Links to", m_targets);
    refForm->addRow("", m_multi);
    refForm->addRow("Other side", m_inverse);
    refForm->addRow("", m_inverseHint);
    m_options->addWidget(ref);
    form->addRow(m_options);

    auto *right = new QVBoxLayout;
    auto *detailTitle = new QLabel("FIELD");
    detailTitle->setObjectName("heading");
    right->addWidget(detailTitle);
    right->addWidget(m_detail);
    right->addStretch();
    body->addLayout(right, 1);

    // --- wiring
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        if (!m_name->text().trimmed().isEmpty()) m_world->renameType(m_type, m_name->text().trimmed());
    });
    connect(m_color, &QPushButton::clicked, this, [this] {
        const auto t = m_world->type(m_type);
        const QColor c = QColorDialog::getColor(typeColor(t ? t->color : QString()), this, "Type color");
        if (c.isValid()) m_world->setTypeColor(m_type, c.name());
    });
    connect(m_fields, &QListWidget::currentRowChanged, this, &TypeEditor::refreshField);
    auto moveBy = [this](int d) {
        const Id f = currentField();
        const int row = m_fields->currentRow();
        if (!f) return;
        m_world->moveField(f, row + d);
        m_fields->setCurrentRow(row + d);
    };
    connect(m_up, &QPushButton::clicked, this, [moveBy] { moveBy(-1); });
    connect(m_down, &QPushButton::clicked, this, [moveBy] { moveBy(1); });
    connect(m_delete, &QPushButton::clicked, this, [this] {
        const auto f = m_world->field(currentField());
        if (!f) return;
        if (QMessageBox::question(this, "Delete field",
                                  QStringLiteral("Delete “%1” from every %2, with everything recorded in it?")
                                      .arg(f->name, m_world->type(m_type)->name)) == QMessageBox::Yes)
            m_world->deleteField(f->id);
    });
    connect(m_fieldName, &QLineEdit::editingFinished, this, [this] {
        const QString n = m_fieldName->text().trimmed();
        if (!n.isEmpty()) saveField([n](FieldDef &f) { f.name = n; });
    });
    connect(m_kind, &QComboBox::activated, this, [this](int i) {
        const Kind k = kindFromKey(m_kind->itemData(i).toString());
        const auto f = m_world->field(currentField());
        if (!f || f->kind == k) return;
        if (f->kind == Kind::Reference &&
            QMessageBox::question(this, "Change kind", "Its links go away when it stops being a link field. Change it?") != QMessageBox::Yes) {
            refreshField();
            return;
        }
        saveField([k](FieldDef &f) { f.kind = k; });
    });
    connect(m_book, &QComboBox::activated, this, [this](int i) {
        const Id b = m_book->itemData(i).toLongLong();
        saveField([b](FieldDef &f) { f.book = b; });
    });
    connect(addStep, &QPushButton::clicked, this, [this] {
        saveField([](FieldDef &f) {
            int next = 1;
            for (const auto &s : f.steps) next = std::max(next, s.id + 1);
            f.steps.append({next, QStringLiteral("Step %1").arg(f.steps.size() + 1)});
        });
        m_steps->setCurrentRow(m_steps->count() - 1);
        m_steps->editItem(m_steps->currentItem());
    });
    connect(m_steps, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_loading) return;
        const int row = m_steps->row(item);
        const QString n = item->text().trimmed();
        QMetaObject::invokeMethod(this, [this, row, n] {
            saveField([row, n](FieldDef &f) {
                if (row >= 0 && row < f.steps.size() && !n.isEmpty()) f.steps[row].name = n;
            });
        }, Qt::QueuedConnection);
    });
    auto moveStep = [this](int d) {
        const int row = m_steps->currentRow();
        saveField([row, d](FieldDef &f) {
            if (row < 0 || row + d < 0 || row + d >= f.steps.size()) return;
            f.steps.swapItemsAt(row, row + d);
        });
        m_steps->setCurrentRow(row + d);
    };
    connect(stepUp, &QPushButton::clicked, this, [moveStep] { moveStep(-1); });
    connect(stepDown, &QPushButton::clicked, this, [moveStep] { moveStep(1); });
    connect(delStep, &QPushButton::clicked, this, [this] {
        const int row = m_steps->currentRow();
        saveField([row](FieldDef &f) {
            if (row >= 0 && row < f.steps.size()) f.steps.removeAt(row);
        });
    });
    connect(m_targets, &QListWidget::itemChanged, this, [this](QListWidgetItem *) {
        if (m_loading) return;
        QList<Id> targets;
        for (int i = 0; i < m_targets->count(); ++i)
            if (m_targets->item(i)->checkState() == Qt::Checked) targets << m_targets->item(i)->data(Qt::UserRole).toLongLong();
        QMetaObject::invokeMethod(this, [this, targets] { saveField([targets](FieldDef &f) { f.targets = targets; }); },
                                  Qt::QueuedConnection);
    });
    connect(m_multi, &QCheckBox::clicked, this, [this](bool on) { saveField([on](FieldDef &f) { f.multi = on; }); });
    connect(m_inverse, &QLineEdit::editingFinished, this, [this] {
        const auto f = m_world->field(currentField());
        if (!f) return;
        const auto partner = m_world->field(f->inverse);
        const QString now = partner ? partner->name : QString();
        if (m_inverse->text().trimmed() != now) m_world->setInverse(f->id, m_inverse->text());
    });

    connect(world, &World::changed, this, &TypeEditor::refresh);
    refresh();
    if (m_fields->count()) m_fields->setCurrentRow(0);
}

Id TypeEditor::currentField() const
{
    auto *item = m_fields->currentItem();
    return item ? item->data(Qt::UserRole).toLongLong() : 0;
}

void TypeEditor::saveField(const std::function<void(FieldDef &)> &change)
{
    auto f = m_world->field(currentField());
    if (!f) return;
    change(*f);
    m_world->updateField(*f);
}

void TypeEditor::refresh()
{
    const auto t = m_world->type(m_type);
    if (!t) {
        emit gone();
        return;
    }
    emit titleChanged(t->name);
    if (!m_name->hasFocus()) m_name->setText(t->name);
    m_color->setStyleSheet(QStringLiteral("QPushButton { background: %1; border-radius: 14px; border: none; }").arg(typeColor(t->color).name()));

    const Id keep = currentField();
    {
        QSignalBlocker b(m_fields);
        m_fields->clear();
        for (const auto &f : m_world->fields(m_type)) {
            QString kind = kindLabel(f.kind);
            if (f.book) kind += " · " + (m_world->book(f.book) ? m_world->book(f.book)->name : QString());
            auto *item = new QListWidgetItem(f.name);
            item->setData(Qt::UserRole, f.id);
            item->setData(Qt::UserRole + 1, kind);
            m_fields->addItem(item);
            if (f.id == keep) m_fields->setCurrentItem(item);
        }
        if (!m_fields->currentItem() && m_fields->count()) m_fields->setCurrentRow(0);
    }
    refreshField();
}

void TypeEditor::refreshField()
{
    const auto f = m_world->field(currentField());
    m_detail->setEnabled(bool(f));
    const int row = m_fields->currentRow();
    m_up->setEnabled(f && row > 0);
    m_down->setEnabled(f && row + 1 < m_fields->count());
    m_delete->setEnabled(bool(f));
    if (!f) {
        m_fieldName->clear();
        m_options->setCurrentIndex(0);
        return;
    }
    m_loading = true;
    if (!m_fieldName->hasFocus()) m_fieldName->setText(f->name);
    m_kind->setCurrentIndex(m_kind->findData(kindKey(f->kind)));
    m_kindHint->setText(kindHint(f->kind));
    m_book->clear();
    m_book->addItem("Every book", Id(0));
    for (const auto &b : m_world->books()) m_book->addItem("Only " + b.name, b.id);
    m_book->setCurrentIndex(std::max(0, m_book->findData(f->book)));

    if (f->kind == Kind::Ladder) {
        m_options->setCurrentIndex(1);
        const int keepStep = m_steps->currentRow();
        m_steps->clear();
        for (const auto &s : f->steps) {
            auto *item = new QListWidgetItem(s.name);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            m_steps->addItem(item);
        }
        if (keepStep >= 0 && keepStep < m_steps->count()) m_steps->setCurrentRow(keepStep);
    } else if (f->kind == Kind::Reference) {
        m_options->setCurrentIndex(2);
        m_targets->clear();
        for (const auto &t : m_world->types()) {
            auto *item = new QListWidgetItem(t.name);
            item->setData(Qt::UserRole, t.id);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(f->targets.contains(t.id) ? Qt::Checked : Qt::Unchecked);
            m_targets->addItem(item);
        }
        m_multi->setChecked(f->multi);
        const auto partner = m_world->field(f->inverse);
        if (!m_inverse->hasFocus()) m_inverse->setText(partner ? partner->name : QString());
        QString hint;
        if (partner && partner->id == f->id) {
            hint = "Symmetric: linking A to B shows on B too, in this same field.";
        } else if (partner) {
            const auto pt = m_world->type(partner->type);
            hint = QStringLiteral("Shows on the %1 sheet as “%2”.").arg(pt ? pt->name : QString(), partner->name);
        } else {
            const Id target = f->targets.isEmpty() ? f->type : f->targets.first();
            const auto tt = m_world->type(target);
            hint = QStringLiteral("Name the field the link shows as on the %1 side; it is created if needed. "
                                  "Use this field's own name to make it symmetric.")
                       .arg(tt ? tt->name : QString("other"));
        }
        m_inverseHint->setText(hint);
    } else {
        m_options->setCurrentIndex(0);
    }
    m_loading = false;
}

} // namespace worldsea

#include "moc_typeeditor.cpp"
