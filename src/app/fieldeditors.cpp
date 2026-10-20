#include "app/fieldeditors.h"

#include "app/flowlayout.h"
#include "app/theme.h"

#include <QCompleter>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QStringListModel>
#include <QToolButton>

namespace worldsea {

// --- Chip ---------------------------------------------------------------------

Chip::Chip(const QString &text, const QColor &color, bool removable, QWidget *parent) : QWidget(parent), m_color(color)
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(9, 3, removable ? 3 : 9, 3);
    row->setSpacing(2);
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::RichText);
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(label);
    if (removable) {
        auto *x = new QToolButton;
        x->setText("×");
        x->setToolTip("Remove");
        x->setCursor(Qt::PointingHandCursor);
        x->setStyleSheet("QToolButton { padding: 0 4px; }");
        connect(x, &QToolButton::clicked, this, &Chip::removeClicked);
        row->addWidget(x);
    }
    setCursor(Qt::PointingHandCursor);
}

void Chip::setDim(bool dim)
{
    m_dim = dim;
    update();
}

void Chip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor fill = m_color;
    fill.setAlphaF(m_dim ? 0.06 : 0.16);
    QColor edge = m_color;
    edge.setAlphaF(m_dim ? 0.3 : 0.75);
    p.setPen(QPen(edge, 1));
    p.setBrush(fill);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
}

void Chip::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->pos())) emit clicked();
}

void Chip::contextMenuEvent(QContextMenuEvent *e) { emit menuRequested(e->globalPos()); }

// --- GrowingTextEdit -----------------------------------------------------------

GrowingTextEdit::GrowingTextEdit(QWidget *parent) : QTextEdit(parent)
{
    setAcceptRichText(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(document(), &QTextDocument::contentsChanged, this, &GrowingTextEdit::fit);
    fit();
}

void GrowingTextEdit::setTextQuietly(const QString &text)
{
    m_last = text;
    if (toPlainText() != text) setPlainText(text);
}

void GrowingTextEdit::focusOutEvent(QFocusEvent *e)
{
    QTextEdit::focusOutEvent(e);
    const QString t = toPlainText();
    if (t != m_last) {
        m_last = t;
        emit committed(t);
    }
}

void GrowingTextEdit::resizeEvent(QResizeEvent *e)
{
    QTextEdit::resizeEvent(e);
    fit();
}

void GrowingTextEdit::fit()
{
    document()->setTextWidth(viewport()->width());
    const int h = int(document()->size().height()) + frameWidth() * 2 + 14;
    setFixedHeight(std::max(h, fontMetrics().lineSpacing() * 3 + 16));
}

// --- TagsEditor ----------------------------------------------------------------

TagsEditor::TagsEditor(QWidget *parent) : QWidget(parent)
{
    m_flow = new FlowLayout(this, 6, 6);
    m_flow->setContentsMargins(0, 0, 0, 0);
    m_input = new QLineEdit;
    m_input->setPlaceholderText("+ tag");
    m_input->setObjectName("ghost");
    m_input->setFixedWidth(120);
    m_flow->addWidget(m_input);
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        const QString t = m_input->text().trimmed();
        m_input->clear();
        if (t.isEmpty() || m_tags.contains(t, Qt::CaseInsensitive)) return;
        m_tags << t;
        rebuild();
        emit edited(m_tags);
    });
}

void TagsEditor::setTags(const QStringList &tags)
{
    if (tags == m_tags) return;
    m_tags = tags;
    rebuild();
}

void TagsEditor::rebuild()
{
    while (m_flow->count() > 1) {
        QLayoutItem *it = m_flow->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    for (int i = 0; i < m_tags.size(); ++i) {
        auto *chip = new Chip(m_tags[i].toHtmlEscaped(), theme().muted, true);
        connect(chip, &Chip::removeClicked, this, [this, t = m_tags[i]] {
            m_tags.removeAll(t);
            rebuild();
            emit edited(m_tags);
        });
        m_flow->insertWidget(i, chip);
    }
}

// --- RefEditor -------------------------------------------------------------------

RefEditor::RefEditor(World *world, Id entity, const FieldDef &field, QWidget *parent)
    : QWidget(parent), m_world(world), m_entity(entity), m_field(field)
{
    m_flow = new FlowLayout(this, 6, 6);
    m_flow->setContentsMargins(0, 0, 0, 0);
    m_input = new QLineEdit;
    m_input->setObjectName("ghost");
    m_input->setFixedWidth(170);
    m_names = new QStringListModel(this);
    auto *completer = new QCompleter(m_names, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    m_input->setCompleter(completer);
    m_flow->addWidget(m_input);
    connect(m_input, &QLineEdit::returnPressed, this, &RefEditor::submit);
    connect(completer, qOverload<const QString &>(&QCompleter::activated), this, [this](const QString &t) {
        m_input->setText(t);
        submit();
    });
    refresh();
}

QList<Id> RefEditor::targetTypes() const
{
    if (!m_field.targets.isEmpty()) return m_field.targets;
    QList<Id> all;
    for (const auto &t : m_world->types()) all << t.id;
    return all;
}

void RefEditor::refresh()
{
    if (auto f = m_world->field(m_field.id)) m_field = *f;
    while (m_flow->count() > 1) {
        QLayoutItem *it = m_flow->takeAt(0);
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    const Id cursor = m_world->cursor();
    const auto links = m_world->links(m_entity, m_field.id);
    QSet<Id> linked;
    int i = 0;
    for (const auto &v : links) {
        const auto other = m_world->entity(v.other);
        if (!other) continue;
        linked.insert(v.other);
        const auto t = m_world->type(other->type);
        QString text = other->name.toHtmlEscaped();
        if (!v.link.note.isEmpty())
            text += QStringLiteral(" <span style='color:#8a8a8a'>· %1</span>").arg(v.link.note.toHtmlEscaped());
        auto *chip = new Chip(text, typeColor(t ? t->color : QString()), true);
        chip->setDim(!m_world->existsAt(*other, cursor));
        QStringList tip{t ? t->name : QString()};
        if (!v.link.note.isEmpty()) tip << v.link.note;
        tip << (v.link.start ? "Since " + m_world->chapterLabel(v.link.start) : QStringLiteral("From the start"));
        if (v.link.end) tip << "Until " + m_world->chapterLabel(v.link.end);
        chip->setToolTip(tip.join('\n'));
        connect(chip, &Chip::clicked, this, [this, id = v.other] { emit openEntity(id); });
        connect(chip, &Chip::removeClicked, this, [this, id = v.link.id] { m_world->removeLink(id); });
        connect(chip, &Chip::menuRequested, this, [this, v](const QPoint &p) { chipMenu(v, p); });
        m_flow->insertWidget(i++, chip);
    }
    // a single-valued field still offers to replace its link
    m_input->setPlaceholderText(links.isEmpty() ? QStringLiteral("+ link") : m_field.multi ? QStringLiteral("+ another") : QStringLiteral("change…"));

    QStringList names;
    const QList<Id> types = targetTypes();
    for (const auto &e : m_world->entities()) {
        if (!types.contains(e.type) || e.id == m_entity || linked.contains(e.id)) continue;
        names << e.name;
        for (const auto &a : e.aliases) names << a;
    }
    names.removeDuplicates();
    m_names->setStringList(names);
}

void RefEditor::linkTo(Id other)
{
    World::Step step(*m_world, "Link " + m_field.name);
    m_world->addLink(m_field.id, m_entity, other);
    if (const auto e = m_world->entity(m_entity); e && e->stub) m_world->setStub(m_entity, false);
}

void RefEditor::submit()
{
    const QString text = m_input->text().trimmed();
    if (text.isEmpty()) return;
    const QList<Id> types = targetTypes();
    if (const auto e = m_world->findEntity(text, types); e && e->id != m_entity) {
        m_input->clear();
        linkTo(e->id);
        return;
    }
    // nobody by that name yet: offer a placeholder to fill in later
    QMenu menu;
    for (Id t : types) {
        const auto type = m_world->type(t);
        if (!type) continue;
        menu.addAction(QStringLiteral("Create %1 “%2”").arg(type->name, text), this, [this, t, text] {
            World::Step step(*m_world, "Create and link");
            const Id id = m_world->addEntity(t, text, true);
            linkTo(id);
            m_input->clear();
        });
    }
    if (menu.isEmpty()) return;
    menu.setActiveAction(menu.actions().first());
    menu.exec(m_input->mapToGlobal(QPoint(0, m_input->height())));
}

void RefEditor::chipMenu(const LinkView &v, const QPoint &globalPos)
{
    QMenu menu;
    menu.addAction("Open", this, [this, id = v.other] { emit openEntity(id); });
    menu.addAction("Note…", this, [this, v] {
        bool ok = false;
        const QString note = QInputDialog::getText(this, "Link note", "A word on this link (a role, how they met):",
                                                   QLineEdit::Normal, v.link.note, &ok);
        if (ok) m_world->setLinkNote(v.link.id, note.trimmed());
    });
    auto chapterMenu = [&](QMenu *m, const QString &none, Id current, auto apply) {
        m->addAction(none, this, [=] { apply(Id(0)); })->setCheckable(true);
        m->actions().last()->setChecked(current == 0);
        for (const auto &b : m_world->books()) {
            m->addSection(b.name);
            for (const auto &c : m_world->chapters(b.id)) {
                auto *a = m->addAction(c.title, this, [=] { apply(c.id); });
                a->setCheckable(true);
                a->setChecked(c.id == current);
            }
        }
    };
    chapterMenu(menu.addMenu("Holds since"), "From the start", v.link.start,
                [this, v](Id ch) { m_world->setLinkSpan(v.link.id, ch, v.link.end); });
    chapterMenu(menu.addMenu("Ends in"), "Still holds", v.link.end,
                [this, v](Id ch) { m_world->setLinkSpan(v.link.id, v.link.start, ch); });
    menu.addSeparator();
    const bool ends = m_world->editMode() == EditMode::Change && m_world->cursor() &&
                      m_world->ordinal(m_world->cursor()) > m_world->ordinal(v.link.start);
    menu.addAction(ends ? "End it in this chapter" : "Remove", this, [this, id = v.link.id] { m_world->removeLink(id); });
    if (ends) menu.addAction("Delete it from every chapter", this, [this, id = v.link.id] { m_world->deleteLinkRow(id); });
    menu.exec(globalPos);
}

} // namespace worldsea

#include "moc_fieldeditors.cpp"
