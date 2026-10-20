#include "app/sheetview.h"

#include "app/fieldeditors.h"
#include "app/flowlayout.h"
#include "app/theme.h"

#include <QBuffer>
#include <QComboBox>
#include <QDoubleValidator>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

namespace worldsea {

namespace {

constexpr int kChapterRole = Qt::UserRole;
constexpr int kRowRole = Qt::UserRole + 1;
constexpr int kWhatRole = Qt::UserRole + 2;

QString chapterTitle(const World &w, Id ch)
{
    const auto c = w.chapter(ch);
    return c ? c->title : QStringLiteral("the start");
}

// an image no wider or taller than this is kept as it is
constexpr int kMaxImageSide = 1600;

QByteArray loadImage(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) return {};
    if (img.width() > kMaxImageSide || img.height() > kMaxImageSide)
        img = img.scaled(kMaxImageSide, kMaxImageSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, img.hasAlphaChannel() ? "PNG" : "JPG", 90);
    return out;
}

} // namespace

SheetView::SheetView(World *world, Id entity, QWidget *parent) : QWidget(parent), m_world(world), m_entity(entity)
{
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    outer->addWidget(split);

    // --- the sheet
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *page = new QWidget;
    page->setObjectName("room");
    scroll->setWidget(page);
    auto *pageRow = new QHBoxLayout(page);
    pageRow->setContentsMargins(28, 22, 28, 28);
    auto *col = new QVBoxLayout;
    col->setSpacing(8);
    pageRow->addLayout(col, 1);
    auto *body = new QWidget;
    body->setMaximumWidth(820);
    auto *bodyCol = new QVBoxLayout(body);
    bodyCol->setContentsMargins(0, 0, 0, 0);
    bodyCol->setSpacing(8);
    col->addWidget(body);
    col->addStretch();

    auto *topRow = new QHBoxLayout;
    m_typeChip = new QLabel;
    m_typeChip->setCursor(Qt::PointingHandCursor);
    m_typeChip->setToolTip("Edit this type's sheet");
    connect(m_typeChip, &QLabel::linkActivated, this, [this] {
        if (const auto e = m_world->entity(m_entity)) emit openType(e->type);
    });
    m_stubNote = new QLabel("placeholder: made from a link, nothing filled in yet");
    m_stubNote->setObjectName("muted");
    topRow->addWidget(m_typeChip);
    topRow->addWidget(m_stubNote);
    topRow->addStretch();
    bodyCol->addLayout(topRow);

    m_name = new QLineEdit;
    m_name->setObjectName("nameEdit");
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        const QString n = m_name->text().trimmed();
        const auto e = m_world->entity(m_entity);
        if (!e) return;
        if (n.isEmpty()) {
            m_name->setText(e->name);
            return;
        }
        World::Step s(*m_world, "Rename item");
        m_world->renameEntity(m_entity, n);
        if (e->stub) m_world->setStub(m_entity, false);
    });
    bodyCol->addWidget(m_name);

    auto *meta = new QGridLayout;
    meta->setHorizontalSpacing(14);
    meta->setVerticalSpacing(6);
    auto label = [](const QString &t) {
        auto *l = new QLabel(t);
        l->setObjectName("fieldName");
        return l;
    };
    m_aliases = new QLineEdit;
    m_aliases->setPlaceholderText("Other names, separated by commas");
    connect(m_aliases, &QLineEdit::editingFinished, this, [this] {
        m_world->setAliases(m_entity, m_aliases->text().split(','));
    });
    m_intro = new QComboBox;
    m_intro->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(m_intro, &QComboBox::activated, this, [this](int i) {
        m_world->setIntro(m_entity, m_intro->itemData(i).toLongLong());
    });
    m_absentNote = new QLabel;
    m_absentNote->setObjectName("muted");
    auto *introRow = new QHBoxLayout;
    introRow->addWidget(m_intro);
    introRow->addWidget(m_absentNote);
    introRow->addStretch();
    meta->addWidget(label("Also known as"), 0, 0);
    meta->addWidget(m_aliases, 0, 1);
    meta->addWidget(label("First appears"), 1, 0);
    meta->addLayout(introRow, 1, 1);
    meta->setColumnStretch(1, 1);
    meta->setColumnMinimumWidth(0, 120);
    bodyCol->addLayout(meta);

    auto *rule = new QFrame;
    rule->setFrameShape(QFrame::HLine);
    rule->setStyleSheet(QStringLiteral("color: %1;").arg(theme().line.name()));
    bodyCol->addSpacing(6);
    bodyCol->addWidget(rule);
    bodyCol->addSpacing(6);

    m_fieldsHost = new QWidget;
    m_grid = new QGridLayout(m_fieldsHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(14);
    m_grid->setVerticalSpacing(10);
    m_grid->setColumnStretch(1, 1);
    m_grid->setColumnMinimumWidth(0, 120);
    // the editors keep their width when a "since" note comes or goes
    m_grid->setColumnMinimumWidth(2, 210);
    bodyCol->addWidget(m_fieldsHost);

    m_backlinksHost = new QWidget;
    m_backlinks = new QVBoxLayout(m_backlinksHost);
    m_backlinks->setContentsMargins(0, 14, 0, 0);
    m_backlinks->setSpacing(8);
    bodyCol->addWidget(m_backlinksHost);

    split->addWidget(scroll);

    // --- the history
    auto *hist = new QWidget;
    hist->setObjectName("history");
    hist->setAttribute(Qt::WA_StyledBackground);
    auto *histCol = new QVBoxLayout(hist);
    histCol->setContentsMargins(12, 14, 12, 12);
    auto *histTitle = new QLabel("HISTORY");
    histTitle->setObjectName("heading");
    m_historyField = new QComboBox;
    connect(m_historyField, &QComboBox::activated, this, &SheetView::refreshHistory);
    m_history = new QListWidget;
    m_history->setWordWrap(true);
    dropFocusFrame(m_history);
    m_history->setContextMenuPolicy(Qt::CustomContextMenu);
    m_history->setToolTip("Double-click to view the world as of that chapter");
    connect(m_history, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *i) {
        const Id ch = i->data(kChapterRole).toLongLong();
        if (ch) m_world->setCursor(ch);
        else if (!m_world->timeline().isEmpty()) m_world->setCursor(m_world->timeline().first());
    });
    connect(m_history, &QListWidget::customContextMenuRequested, this, &SheetView::historyMenu);
    histCol->addWidget(histTitle);
    histCol->addWidget(m_historyField);
    histCol->addWidget(m_history, 1);
    split->addWidget(hist);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    split->setSizes({700, 280});

    connect(world, &World::changed, this, &SheetView::refresh);
    connect(world, &World::cursorChanged, this, &SheetView::refresh);
    connect(world, &World::editModeChanged, this, &SheetView::refresh);
    refresh();
}

void SheetView::focusName()
{
    m_name->setFocus();
    m_name->selectAll();
}

QString SheetView::structureKey() const
{
    const auto e = m_world->entity(m_entity);
    if (!e) return {};
    QStringList parts{QString::number(e->type)};
    for (const auto &f : m_world->fields(e->type, m_world->cursorBook())) {
        QStringList steps;
        for (const auto &s : f.steps) steps << QString::number(s.id) + s.name;
        QStringList targets;
        for (Id t : f.targets) targets << QString::number(t);
        parts << QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
                     .arg(f.id)
                     .arg(f.name, kindKey(f.kind), steps.join(','), targets.join(','))
                     .arg(f.multi)
                     .arg(f.inverse);
    }
    return parts.join(';');
}

void SheetView::refresh()
{
    const auto e = m_world->entity(m_entity);
    if (!e) {
        emit gone();
        return;
    }
    const auto t = m_world->type(e->type);
    const QColor color = typeColor(t ? t->color : QString());
    m_typeChip->setText(QStringLiteral("<a href='type' style='color:%1; text-decoration:none'>● %2</a>")
                            .arg(color.name(), (t ? t->name : QString()).toHtmlEscaped()));
    m_stubNote->setVisible(e->stub);
    if (!m_name->hasFocus()) m_name->setText(e->name);
    if (!m_aliases->hasFocus()) m_aliases->setText(e->aliases.join(", "));
    emit titleChanged(e->name);

    {
        QSignalBlocker b(m_intro);
        m_intro->clear();
        m_intro->addItem("From the start", Id(0));
        for (Id ch : m_world->timeline()) m_intro->addItem(m_world->chapterLabel(ch), ch);
        m_intro->setCurrentIndex(std::max(0, m_intro->findData(e->intro)));
    }
    const bool present = m_world->existsAt(*e, m_world->cursor());
    m_absentNote->setText(present ? QString() : QStringLiteral("not in the story yet as of this chapter"));

    const QString key = structureKey();
    if (key != m_structure) {
        m_structure = key;
        buildFields();
    } else {
        for (auto &r : m_rows) {
            r.refresh();
            refreshSince(r);
        }
    }
    refreshBacklinks();
    refreshHistory();
}

void SheetView::setValue(const FieldDef &f, const QString &value)
{
    World::Step s(*m_world, "Edit " + f.name);
    m_world->setValue(m_entity, f.id, value);
    if (const auto e = m_world->entity(m_entity); e && e->stub) m_world->setStub(m_entity, false);
}

void SheetView::buildFields()
{
    while (QLayoutItem *it = m_grid->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    m_rows.clear();
    const auto e = m_world->entity(m_entity);
    if (!e) return;
    const auto fields = m_world->fields(e->type, m_world->cursorBook());
    m_rows.reserve(fields.size());
    int r = 0;
    for (const auto &f : fields) {
        m_rows.append(Row{f, nullptr, [] {}});
        Row &row = m_rows.last();
        auto *name = new QLabel(f.name);
        name->setObjectName("fieldName");
        if (f.book) name->setToolTip("Only in " + (m_world->book(f.book) ? m_world->book(f.book)->name : QString()));
        QWidget *editor = editorFor(f, row);
        row.since = new QLabel;
        row.since->setObjectName("since");
        row.since->setTextFormat(Qt::RichText);
        connect(row.since, &QLabel::linkActivated, this, [this, id = f.id] {
            m_historyField->setCurrentIndex(std::max(0, m_historyField->findData(id)));
            refreshHistory();
        });
        const Qt::Alignment top = Qt::AlignTop;
        m_grid->addWidget(name, r, 0, top | Qt::AlignLeft);
        m_grid->addWidget(editor, r, 1, top);
        m_grid->addWidget(row.since, r, 2, top | Qt::AlignLeft);
        // labels line up with the text inside the editors
        name->setContentsMargins(0, 7, 0, 0);
        row.since->setContentsMargins(0, 7, 0, 0);
        ++r;
    }
    if (fields.isEmpty()) {
        auto *none = new QLabel(QStringLiteral("This type has no fields yet. <a href='edit' style='color:%1'>Add some</a>")
                                    .arg(theme().accent.name()));
        none->setObjectName("muted");
        connect(none, &QLabel::linkActivated, this, [this, type = e->type] { emit openType(type); });
        m_grid->addWidget(none, 0, 0, 1, 3);
    }
    for (auto &row : m_rows) {
        row.refresh();
        refreshSince(row);
    }
}

QWidget *SheetView::editorFor(const FieldDef &f, Row &row)
{
    const Id fid = f.id;
    switch (f.kind) {
    case Kind::Text:
    case Kind::Number: {
        auto *ed = new QLineEdit;
        if (f.kind == Kind::Number) {
            auto *v = new QDoubleValidator(ed);
            v->setNotation(QDoubleValidator::StandardNotation);
            ed->setValidator(v);
            ed->setMaximumWidth(180);
        }
        connect(ed, &QLineEdit::editingFinished, this, [this, ed, f] {
            if (ed->text() != m_world->effective(m_entity, f.id).value) setValue(f, ed->text().trimmed());
        });
        row.refresh = [this, ed, fid] {
            if (!ed->hasFocus()) ed->setText(m_world->effective(m_entity, fid).value);
        };
        return ed;
    }
    case Kind::LongText: {
        auto *ed = new GrowingTextEdit;
        connect(ed, &GrowingTextEdit::committed, this, [this, f](const QString &t) { setValue(f, t); });
        row.refresh = [this, ed, fid] {
            if (!ed->hasFocus()) ed->setTextQuietly(m_world->effective(m_entity, fid).value);
        };
        return ed;
    }
    case Kind::Ladder: {
        auto *box = new QComboBox;
        box->addItem("—", QString());
        for (const auto &s : f.steps) box->addItem(s.name, QString::number(s.id));
        box->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        auto *wrap = new QWidget;
        auto *l = new QHBoxLayout(wrap);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(box);
        auto *rank = new QLabel;
        rank->setObjectName("muted");
        l->addWidget(rank);
        l->addStretch();
        connect(box, &QComboBox::activated, this, [this, box, f](int i) { setValue(f, box->itemData(i).toString()); });
        row.refresh = [this, box, rank, fid, n = int(f.steps.size())] {
            const QString v = m_world->effective(m_entity, fid).value;
            QSignalBlocker b(box);
            const int i = box->findData(v);
            box->setCurrentIndex(std::max(0, i));
            rank->setText(i > 0 ? QStringLiteral("%1 of %2").arg(i).arg(n) : QString());
        };
        return wrap;
    }
    case Kind::Tags: {
        auto *ed = new TagsEditor;
        connect(ed, &TagsEditor::edited, this, [this, f](const QStringList &tags) { setValue(f, tags.join('\n')); });
        row.refresh = [this, ed, fid] { ed->setTags(m_world->effective(m_entity, fid).value.split('\n', Qt::SkipEmptyParts)); };
        return ed;
    }
    case Kind::Image: {
        auto *wrap = new QWidget;
        auto *l = new QHBoxLayout(wrap);
        l->setContentsMargins(0, 0, 0, 0);
        auto *pic = new QLabel;
        pic->setFixedSize(160, 160);
        pic->setAlignment(Qt::AlignCenter);
        pic->setStyleSheet(QStringLiteral("border: 1px dashed %1; border-radius: 6px;").arg(theme().line.name()));
        auto *buttons = new QVBoxLayout;
        auto *choose = new QPushButton("Choose…");
        auto *clear = new QPushButton("Remove");
        buttons->addWidget(choose);
        buttons->addWidget(clear);
        buttons->addStretch();
        l->addWidget(pic);
        l->addLayout(buttons);
        l->addStretch();
        connect(choose, &QPushButton::clicked, this, [this, f] {
            const QString path = QFileDialog::getOpenFileName(this, "Choose an image", {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)");
            if (path.isEmpty()) return;
            const QByteArray data = loadImage(path);
            if (data.isEmpty()) return;
            World::Step s(*m_world, "Set " + f.name);
            setValue(f, QString::number(m_world->addBlob(data)));
        });
        connect(clear, &QPushButton::clicked, this, [this, f] { setValue(f, {}); });
        row.refresh = [this, pic, clear, fid] {
            const QString v = m_world->effective(m_entity, fid).value;
            QImage img;
            if (!v.isEmpty()) img = QImage::fromData(m_world->blob(v.toLongLong()));
            if (img.isNull()) {
                pic->setPixmap({});
                pic->setText("no image");
            } else {
                pic->setPixmap(QPixmap::fromImage(img.scaled(158, 158, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
            clear->setEnabled(!img.isNull());
        };
        return wrap;
    }
    case Kind::Reference: {
        auto *ed = new RefEditor(m_world, m_entity, f);
        connect(ed, &RefEditor::openEntity, this, &SheetView::openEntity);
        row.refresh = [ed] { ed->refresh(); };
        return ed;
    }
    }
    return new QWidget;
}

void SheetView::refreshSince(Row &row)
{
    const Id cursor = m_world->cursor();
    QStringList parts;
    QStringList tip;
    const QString link = QStringLiteral("<a href='h' style='color:%1; text-decoration:none'>%2</a>");
    if (row.field.kind == Kind::Reference) {
        int later = 0;
        for (const auto &v : m_world->links(m_entity, row.field.id, std::nullopt)) {
            const int at = m_world->ordinal(cursor);
            if (m_world->ordinal(v.link.start) > at) ++later;
            if (v.link.end && m_world->ordinal(v.link.end) > at) ++later;
        }
        if (later) {
            parts << QStringLiteral("changes later");
            tip << QStringLiteral("%1 later changes; see the history").arg(later);
        }
    } else {
        const Effective eff = m_world->effective(m_entity, row.field.id);
        if (eff.has && eff.since && eff.since != cursor) {
            parts << "since " + chapterTitle(*m_world, eff.since);
            tip << "Recorded in " + m_world->chapterLabel(eff.since);
        } else if (eff.has && eff.since == cursor && cursor) {
            parts << "changed here";
        }
        if (eff.nextChange) {
            parts << "changes in " + chapterTitle(*m_world, eff.nextChange);
            tip << "Next change: " + m_world->chapterLabel(eff.nextChange);
        }
    }
    const QColor c = theme().muted;
    row.since->setText(parts.isEmpty() ? QString() : link.arg(c.name(), parts.join(" · ").toHtmlEscaped()));
    row.since->setToolTip(tip.join('\n'));
}

void SheetView::refreshBacklinks()
{
    while (QLayoutItem *it = m_backlinks->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        if (it->layout()) {
            while (QLayoutItem *c = it->layout()->takeAt(0)) {
                if (c->widget()) c->widget()->deleteLater();
                delete c;
            }
        }
        delete it;
    }
    // links that point here from a field with no field on this side to show them
    QMap<QString, QList<Id>> byField;
    for (const auto &l : m_world->allLinks(m_world->cursor())) {
        if (l.dst != m_entity) continue;
        const auto f = m_world->field(l.field);
        if (!f || f->inverse) continue;
        const auto t = m_world->type(f->type);
        byField[QStringLiteral("%1 of (%2)").arg(f->name, t ? t->name : QString())] << l.src;
    }
    m_backlinksHost->setVisible(!byField.isEmpty());
    if (byField.isEmpty()) return;
    auto *title = new QLabel("LINKED FROM");
    title->setObjectName("heading");
    m_backlinks->addWidget(title);
    for (auto it = byField.begin(); it != byField.end(); ++it) {
        auto *row = new QWidget;
        auto *flow = new FlowLayout(row, 6, 6);
        flow->setContentsMargins(0, 0, 0, 0);
        auto *name = new QLabel(it.key());
        name->setObjectName("fieldName");
        flow->addWidget(name);
        for (Id src : it.value()) {
            const auto e = m_world->entity(src);
            if (!e) continue;
            const auto t = m_world->type(e->type);
            auto *chip = new Chip(e->name.toHtmlEscaped(), typeColor(t ? t->color : QString()), false);
            connect(chip, &Chip::clicked, this, [this, src] { emit openEntity(src); });
            flow->addWidget(chip);
        }
        m_backlinks->addWidget(row);
    }
}

void SheetView::refreshHistory()
{
    const auto e = m_world->entity(m_entity);
    if (!e) return;
    const Id keep = m_historyField->currentData().toLongLong();
    {
        QSignalBlocker b(m_historyField);
        m_historyField->clear();
        m_historyField->addItem("Every field", Id(0));
        for (const auto &f : m_world->fields(e->type)) m_historyField->addItem(f.name, f.id);
        m_historyField->setCurrentIndex(std::max(0, m_historyField->findData(keep)));
    }
    const Id only = m_historyField->currentData().toLongLong();

    m_history->clear();
    const Theme &th = theme();
    const int at = m_world->ordinal(m_world->cursor());
    Id lastChapter = -1;
    for (const auto &h : m_world->history(m_entity)) {
        if (only && h.field != only) continue;
        if (h.chapter != lastChapter) {
            lastChapter = h.chapter;
            if (m_history->count()) {
                auto *gap = new QListWidgetItem;
                gap->setFlags(Qt::NoItemFlags);
                gap->setSizeHint(QSize(1, 6));
                m_history->addItem(gap);
            }
            auto *head = new QListWidgetItem(h.chapter ? m_world->chapterLabel(h.chapter) : QStringLiteral("From the start"));
            head->setFlags(Qt::ItemIsEnabled);
            head->setData(kChapterRole, h.chapter);
            QFont f = head->font();
            f.setPixelSize(11);
            f.setBold(true);
            head->setFont(f);
            head->setForeground(h.chapter == m_world->cursor() ? th.accent : th.muted);
            m_history->addItem(head);
        }
        const auto f = m_world->field(h.field);
        const QString fname = f ? f->name : QString();
        QString text;
        switch (h.what) {
        case HistoryEntry::Intro: text = "First appears"; break;
        case HistoryEntry::Value: text = fname + " → " + (f ? displayValue(*m_world, *f, h.value) : h.value); break;
        case HistoryEntry::LinkStart:
        case HistoryEntry::LinkEnd: {
            const auto o = m_world->entity(h.other);
            text = QStringLiteral("%1 %2 %3").arg(fname, h.what == HistoryEntry::LinkStart ? "+" : "−", o ? o->name : QString("?"));
            if (h.what == HistoryEntry::LinkStart && !h.value.isEmpty()) text += " · " + h.value;
            break;
        }
        }
        auto *item = new QListWidgetItem(text);
        item->setData(kChapterRole, h.chapter);
        item->setData(kRowRole, h.rowId);
        item->setData(kWhatRole, int(h.what));
        if (m_world->ordinal(h.chapter) > at) item->setForeground(th.faint);
        m_history->addItem(item);
    }
    if (!m_history->count()) {
        auto *none = new QListWidgetItem("Nothing recorded yet");
        none->setFlags(Qt::NoItemFlags);
        none->setForeground(th.faint);
        m_history->addItem(none);
    }
}

void SheetView::historyMenu(const QPoint &p)
{
    QListWidgetItem *item = m_history->itemAt(p);
    if (!item || !item->data(kRowRole).isValid()) return;
    const Id rowId = item->data(kRowRole).toLongLong();
    const Id ch = item->data(kChapterRole).toLongLong();
    const auto what = HistoryEntry::What(item->data(kWhatRole).toInt());
    QMenu menu;
    menu.addAction("View as of this chapter", this, [this, ch] {
        if (ch) m_world->setCursor(ch);
    })->setEnabled(ch != 0);
    if (what == HistoryEntry::Value) {
        const Id cursor = m_world->cursor();
        menu.addAction("Move to " + chapterTitle(*m_world, cursor), this, [this, rowId, cursor] {
            m_world->moveValueRow(rowId, cursor);
        })->setEnabled(cursor && cursor != ch);
        menu.addAction("Move to the start", this, [this, rowId] { m_world->moveValueRow(rowId, 0); })->setEnabled(ch != 0);
        menu.addSeparator();
        menu.addAction("Delete this change", this, [this, rowId] { m_world->deleteValueRow(rowId); });
    } else if (what == HistoryEntry::LinkStart || what == HistoryEntry::LinkEnd) {
        menu.addSeparator();
        if (what == HistoryEntry::LinkEnd) {
            menu.addAction("Undo the ending (it still holds)", this, [this, rowId] {
                const auto l = m_world->link(rowId);
                if (l) m_world->setLinkSpan(rowId, l->start, 0);
            });
        }
        menu.addAction("Delete this link", this, [this, rowId] { m_world->deleteLinkRow(rowId); });
    } else if (what == HistoryEntry::Intro) {
        menu.addAction("Present from the start", this, [this] { m_world->setIntro(m_entity, 0); });
    }
    menu.exec(m_history->viewport()->mapToGlobal(p));
}

} // namespace worldsea

#include "moc_sheetview.cpp"
