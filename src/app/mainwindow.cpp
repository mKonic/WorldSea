#include "app/mainwindow.h"

#include "app/checksview.h"
#include "app/graphview.h"
#include "app/hierarchyview.h"
#include "app/lanesview.h"
#include "app/matrixview.h"
#include "app/navpane.h"
#include "app/quickopen.h"
#include "app/sheetview.h"
#include "app/tableview.h"
#include "app/timelinebar.h"
#include "app/typeeditor.h"
#include "core/wikiexport.h"

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QListWidget>
#include <QUrl>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTextEdit>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace worldsea {

namespace {

constexpr int kMaxRecent = 10;
const char *kExt = ".worldsea";

QStringList recentWorlds()
{
    QStringList out;
    for (const auto &p : QSettings().value("recent").toStringList())
        if (QFileInfo::exists(p)) out << p;
    return out;
}

void rememberWorld(const QString &path)
{
    QStringList r = QSettings().value("recent").toStringList();
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > kMaxRecent) r.removeLast();
    QSettings().setValue("recent", r);
    QSettings().setValue("lastWorld", path);
}

QString worldsDir()
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs.isEmpty() ? QDir::homePath() : docs).filePath("WorldSea");
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_world(new World(this))
{
    resize(1360, 860);
    if (const QByteArray g = QSettings().value("geometry").toByteArray(); !g.isEmpty()) restoreGeometry(g);

    m_stack = new QStackedWidget;
    setCentralWidget(m_stack);
    m_welcome = buildWelcome();
    m_stack->addWidget(m_welcome);

    m_workspace = new QWidget;
    auto *col = new QVBoxLayout(m_workspace);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    col->addWidget(new TimelineBar(m_world));
    auto *split = new QSplitter(Qt::Horizontal);
    m_nav = new NavPane(m_world);
    m_tabs = new QTabWidget;
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    split->addWidget(m_nav);
    split->addWidget(m_tabs);
    split->setStretchFactor(1, 1);
    split->setSizes({260, 1100});
    col->addWidget(split, 1);
    m_stack->addWidget(m_workspace);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, [this](int i) {
        QWidget *w = m_tabs->widget(i);
        m_tabs->removeTab(i);
        w->deleteLater();
    });
    connect(m_nav, &NavPane::openEntity, this, &MainWindow::openEntity);
    connect(m_nav, &NavPane::openType, this, &MainWindow::openType);
    connect(m_nav, &NavPane::openTable, this, &MainWindow::openTable);
    connect(m_nav, &NavPane::openGraph, this, &MainWindow::openGraph);
    connect(m_nav, &NavPane::openChecks, this, &MainWindow::openChecks);
    connect(m_nav, &NavPane::openHierarchy, this, &MainWindow::openHierarchy);
    connect(m_nav, &NavPane::openLanes, this, &MainWindow::openLanes);
    connect(m_nav, &NavPane::openMatrix, this, &MainWindow::openMatrix);
    connect(m_world, &World::changed, this, &MainWindow::updateTitle);

    buildMenus();
    updateTitle();
    qApp->installEventFilter(this);
}

bool MainWindow::eventFilter(QObject *o, QEvent *e)
{
    // Ctrl+Z in a text box with nothing of its own to undo undoes the world
    if (e->type() == QEvent::ShortcutOverride) {
        auto *k = static_cast<QKeyEvent *>(e);
        const bool undo = k->matches(QKeySequence::Undo);
        const bool redo = k->matches(QKeySequence::Redo) || k->keyCombination() == QKeyCombination(Qt::CTRL | Qt::SHIFT, Qt::Key_Z);
        if (undo || redo) {
            bool own = false;
            if (auto *l = qobject_cast<QLineEdit *>(o)) own = undo ? l->isUndoAvailable() : l->isRedoAvailable();
            else if (auto *t = qobject_cast<QTextEdit *>(o)) own = undo ? t->document()->isUndoAvailable() : t->document()->isRedoAvailable();
            else return false;
            if (!own) {
                e->ignore();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(o, e);
}

QWidget *MainWindow::buildWelcome()
{
    auto *page = new QWidget;
    page->setObjectName("room");
    auto *outer = new QVBoxLayout(page);
    outer->addStretch(2);
    auto *inner = new QVBoxLayout;
    inner->setSpacing(12);
    auto *title = new QLabel("WorldSea");
    title->setObjectName("title");
    title->setStyleSheet("font-size: 30px;");
    auto *sub = new QLabel("The characters, places and powers of your books, and how they change chapter by chapter.");
    sub->setObjectName("muted");
    auto *buttons = new QHBoxLayout;
    auto *create = new QPushButton("New world…");
    create->setObjectName("gold");
    auto *open = new QPushButton("Open world…");
    buttons->addWidget(create);
    buttons->addWidget(open);
    buttons->addStretch();
    auto *recentTitle = new QLabel("RECENT");
    recentTitle->setObjectName("heading");
    m_recentList = new QWidget;
    new QVBoxLayout(m_recentList);
    m_recentList->layout()->setContentsMargins(0, 0, 0, 0);
    m_recentList->layout()->setSpacing(2);
    inner->addWidget(title);
    inner->addWidget(sub);
    inner->addSpacing(8);
    inner->addLayout(buttons);
    inner->addSpacing(18);
    inner->addWidget(recentTitle);
    inner->addWidget(m_recentList);
    auto *center = new QHBoxLayout;
    center->addStretch(1);
    center->addLayout(inner, 2);
    center->addStretch(1);
    outer->addLayout(center);
    outer->addStretch(3);
    connect(create, &QPushButton::clicked, this, &MainWindow::newWorld);
    connect(open, &QPushButton::clicked, this, &MainWindow::chooseWorld);
    return page;
}

void MainWindow::fillRecent()
{
    m_recent->clear();
    auto *list = m_recentList->layout();
    while (QLayoutItem *it = list->takeAt(0)) {
        if (it->widget()) it->widget()->deleteLater();
        delete it;
    }
    const QStringList recent = recentWorlds();
    for (const auto &p : recent) {
        m_recent->addAction(QFileInfo(p).completeBaseName() + "    " + QDir::toNativeSeparators(p), this, [this, p] { openWorld(p); });
        auto *b = new QPushButton(QFileInfo(p).completeBaseName() + "   " + QDir::toNativeSeparators(QFileInfo(p).absolutePath()));
        b->setObjectName("flat");
        b->setStyleSheet("text-align: left;");
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, p] { openWorld(p); });
        list->addWidget(b);
    }
    m_recent->setEnabled(!recent.isEmpty());
    if (recent.isEmpty()) {
        auto *none = new QLabel("Nothing yet.");
        none->setObjectName("muted");
        list->addWidget(none);
    }
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu("&File");
    file->addAction("&New world…", QKeySequence("Ctrl+Shift+N"), this, &MainWindow::newWorld);
    file->addAction("&Open world…", QKeySequence::Open, this, &MainWindow::chooseWorld);
    m_recent = file->addMenu("Open &recent");
    connect(m_recent, &QMenu::aboutToShow, this, &MainWindow::fillRecent);
    file->addSeparator();
    m_worldActions << file->addAction("&Export reader wiki…", this, &MainWindow::exportWikiDialog);
    m_worldActions << file->addAction("&Close world", QKeySequence("Ctrl+Shift+W"), this, &MainWindow::closeWorld);
    file->addSeparator();
    file->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

    QMenu *edit = menuBar()->addMenu("&Edit");
    QAction *undo = edit->addAction("&Undo", QKeySequence::Undo, m_world, &World::undo);
    QAction *redo = edit->addAction("&Redo", QKeySequence("Ctrl+Shift+Z"), m_world, &World::redo);
    auto *redoAlt = new QAction(this);
    redoAlt->setShortcut(QKeySequence("Ctrl+Y"));
    connect(redoAlt, &QAction::triggered, m_world, &World::redo);
    addAction(redoAlt);
    connect(m_world, &World::undoStateChanged, this, [this, undo, redo] {
        undo->setEnabled(m_world->canUndo());
        undo->setText(m_world->canUndo() ? "&Undo " + m_world->undoLabel().toLower() : QStringLiteral("&Undo"));
        redo->setEnabled(m_world->canRedo());
        redo->setText(m_world->canRedo() ? "&Redo " + m_world->redoLabel().toLower() : QStringLiteral("&Redo"));
    });
    undo->setEnabled(false);
    redo->setEnabled(false);
    m_worldActions << undo << redo;
    edit->addSeparator();
    QMenu *mode = edit->addMenu("Edits &land as");
    QAction *correct = mode->addAction("&Corrections", QKeySequence("Ctrl+Shift+1"), this, [this] { m_world->setEditMode(EditMode::Correct); });
    QAction *change = mode->addAction("Changes in this c&hapter", QKeySequence("Ctrl+Shift+2"), this, [this] { m_world->setEditMode(EditMode::Change); });
    correct->setCheckable(true);
    change->setCheckable(true);
    connect(m_world, &World::editModeChanged, this, [this, correct, change] {
        correct->setChecked(m_world->editMode() == EditMode::Correct);
        change->setChecked(m_world->editMode() == EditMode::Change);
    });
    m_worldActions << correct << change;

    QMenu *go = menuBar()->addMenu("&Go");
    m_worldActions << go->addAction("&Quick open…", QKeySequence("Ctrl+K"), this, &MainWindow::quickOpen);
    auto *quickAlt = new QAction(this);
    quickAlt->setShortcut(QKeySequence("Ctrl+P"));
    connect(quickAlt, &QAction::triggered, this, &MainWindow::quickOpen);
    addAction(quickAlt);
    m_worldActions << quickAlt;
    m_worldActions << go->addAction("&Graph", QKeySequence("Ctrl+G"), this, &MainWindow::openGraph);
    m_worldActions << go->addAction("&Table of this type", QKeySequence("Ctrl+T"), this, [this] {
        Id type = 0;
        if (auto *s = qobject_cast<SheetView *>(m_tabs->currentWidget())) {
            if (const auto e = m_world->entity(s->entityId())) type = e->type;
        } else if (auto *t = qobject_cast<TypeEditor *>(m_tabs->currentWidget())) {
            type = t->typeId();
        }
        if (!type && !m_world->types().isEmpty()) type = m_world->types().first().id;
        if (type) openTable(type);
    });
    m_worldActions << go->addAction("Time&line", QKeySequence("Ctrl+L"), this, &MainWindow::openLanes);
    m_worldActions << go->addAction("&Who knows what", QKeySequence("Ctrl+Shift+M"), this, &MainWindow::openMatrix);
    m_worldActions << go->addAction("&Hierarchy", QKeySequence("Ctrl+Shift+H"), this, &MainWindow::openHierarchy);
    m_worldActions << go->addAction("&Continuity checks", QKeySequence("Ctrl+Shift+C"), this, &MainWindow::openChecks);
    m_worldActions << go->addAction("&Filter items", QKeySequence::Find, this, [this] { m_nav->focusFilter(); });
    go->addSeparator();
    m_worldActions << go->addAction("Close &tab", QKeySequence("Ctrl+W"), this, [this] {
        if (m_tabs->currentIndex() >= 0) emit m_tabs->tabCloseRequested(m_tabs->currentIndex());
    });

    QMenu *item = menuBar()->addMenu("&Item");
    m_worldActions << item->addAction("&New item", QKeySequence::New, this, &MainWindow::newItemHere);
    QMenu *newOf = item->addMenu("New &of type");
    connect(newOf, &QMenu::aboutToShow, this, [this, newOf] {
        newOf->clear();
        for (const auto &t : m_world->types()) newOf->addAction(t.name, this, [this, id = t.id] { m_nav->newEntity(id); });
    });
    item->addSeparator();
    m_worldActions << item->addAction("New &type", this, [this] { m_nav->newType(); });
    m_worldActions << item->addAction("New &book", this, [this] { m_nav->newBook(); });
    m_worldActions << item->addAction("New &chapter", this, [this] {
        Id b = m_world->cursorBook();
        if (!b && !m_world->books().isEmpty()) b = m_world->books().last().id;
        if (b) m_nav->newChapter(b);
    });
    m_worldActions << newOf->menuAction();
}

void MainWindow::updateTitle()
{
    const bool open = m_world->isOpen();
    setWindowTitle(open ? m_world->name() + QStringLiteral(" — WorldSea") : QStringLiteral("WorldSea"));
    m_stack->setCurrentWidget(open ? m_workspace : m_welcome);
    for (auto *a : m_worldActions) a->setEnabled(open);
    if (open) {
        // the undo actions follow the world's own state
        emit m_world->undoStateChanged();
        emit m_world->editModeChanged();
    } else {
        fillRecent();
    }
}

bool MainWindow::openWorld(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    closeWorld();
    try {
        m_world->open(abs);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, "Open world", QStringLiteral("Could not open %1:\n%2").arg(QDir::toNativeSeparators(abs), e.what()));
        updateTitle();
        return false;
    }
    rememberWorld(abs);
    updateTitle();
    openGraph();
    return true;
}

void MainWindow::closeWorld()
{
    while (m_tabs->count()) {
        QWidget *w = m_tabs->widget(0);
        m_tabs->removeTab(0);
        delete w;
    }
    if (m_world->isOpen()) {
        m_world->close();
        QSettings().remove("lastWorld");
    }
    updateTitle();
}

void MainWindow::newWorld()
{
    QDir().mkpath(worldsDir());
    QString path = QFileDialog::getSaveFileName(this, "New world", QDir(worldsDir()).filePath("My world.worldsea"),
                                                "WorldSea worlds (*.worldsea)");
    if (path.isEmpty()) return;
    if (!path.endsWith(kExt)) path += kExt;
    // the dialog already asked before replacing it
    if (QFileInfo::exists(path)) {
        closeWorld();
        for (const char *suffix : {"", "-wal", "-shm"}) QFile::remove(path + suffix);
    }
    openWorld(path);
}

void MainWindow::exportWikiDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle("Export reader wiki");
    auto *form = new QFormLayout(&dlg);
    form->setContentsMargins(22, 20, 22, 18);
    form->setVerticalSpacing(10);
    auto *intro = new QLabel("A web page per item, as readers know it at a chapter: nothing later gets in.");
    intro->setObjectName("muted");
    intro->setWordWrap(true);
    form->addRow(intro);
    auto *chapter = new QComboBox;
    for (Id ch : m_world->timeline()) chapter->addItem(m_world->chapterLabel(ch), ch);
    chapter->setCurrentIndex(std::max(0, chapter->findData(m_world->cursor())));
    form->addRow("Readers have reached", chapter);
    auto *types = new QListWidget;
    types->setMaximumHeight(170);
    for (const auto &t : m_world->types()) {
        auto *item = new QListWidgetItem(t.name);
        item->setData(Qt::UserRole, t.id);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(spoilerTypeByDefault(t.name) ? Qt::Unchecked : Qt::Checked);
        types->addItem(item);
    }
    form->addRow("Include", types);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *go = buttons->addButton("Export…", QDialogButtonBox::AcceptRole);
    go->setObjectName("gold");
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted) return;

    WikiExport opt;
    opt.asOf = chapter->currentData().toLongLong();
    for (int i = 0; i < types->count(); ++i)
        if (types->item(i)->checkState() == Qt::Checked) opt.types.insert(types->item(i)->data(Qt::UserRole).toLongLong());
    const QString base = QFileDialog::getExistingDirectory(this, "Export into", QFileInfo(m_world->path()).absolutePath());
    if (base.isEmpty()) return;
    opt.dir = QDir(base).filePath(m_world->name() + " wiki");
    QString error;
    if (exportWiki(*m_world, opt, &error) < 0) {
        QMessageBox::warning(this, "Export reader wiki", "The export stopped: " + error);
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(opt.dir).filePath("index.html")));
}

void MainWindow::chooseWorld()
{
    const QString path = QFileDialog::getOpenFileName(this, "Open world", worldsDir(), "WorldSea worlds (*.worldsea);;All files (*)");
    if (!path.isEmpty()) openWorld(path);
}

void MainWindow::openEntity(Id id, bool focusName)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *s = qobject_cast<SheetView *>(m_tabs->widget(i)); s && s->entityId() == id) {
            m_tabs->setCurrentIndex(i);
            if (focusName) s->focusName();
            return;
        }
    }
    const auto e = m_world->entity(id);
    if (!e) return;
    auto *sheet = new SheetView(m_world, id);
    const int index = m_tabs->addTab(sheet, e->name);
    connect(sheet, &SheetView::openEntity, this, [this](Id other) { openEntity(other); });
    connect(sheet, &SheetView::openType, this, &MainWindow::openType);
    connect(sheet, &SheetView::titleChanged, this, [this, sheet](const QString &t) {
        if (const int i = m_tabs->indexOf(sheet); i >= 0) m_tabs->setTabText(i, t);
    });
    connect(sheet, &SheetView::gone, this, [this, sheet] {
        if (const int i = m_tabs->indexOf(sheet); i >= 0) m_tabs->removeTab(i);
        sheet->deleteLater();
    });
    m_tabs->setCurrentIndex(index);
    if (focusName) sheet->focusName();
}

void MainWindow::openType(Id id)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *t = qobject_cast<TypeEditor *>(m_tabs->widget(i)); t && t->typeId() == id) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    const auto t = m_world->type(id);
    if (!t) return;
    auto *ed = new TypeEditor(m_world, id);
    const int index = m_tabs->addTab(ed, t->name + " sheet");
    connect(ed, &TypeEditor::titleChanged, this, [this, ed](const QString &title) {
        if (const int i = m_tabs->indexOf(ed); i >= 0) m_tabs->setTabText(i, title + " sheet");
    });
    connect(ed, &TypeEditor::gone, this, [this, ed] {
        if (const int i = m_tabs->indexOf(ed); i >= 0) m_tabs->removeTab(i);
        ed->deleteLater();
    });
    m_tabs->setCurrentIndex(index);
}

void MainWindow::openTable(Id id)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *t = qobject_cast<TableView *>(m_tabs->widget(i)); t && t->typeId() == id) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    const auto t = m_world->type(id);
    if (!t) return;
    auto *table = new TableView(m_world, id);
    connect(table, &TableView::openEntity, this, [this](Id e) { openEntity(e); });
    connect(table, &TableView::titleChanged, this, [this, table](const QString &title) {
        if (const int i = m_tabs->indexOf(table); i >= 0) m_tabs->setTabText(i, title + " table");
    });
    connect(table, &TableView::gone, this, [this, table] {
        if (const int i = m_tabs->indexOf(table); i >= 0) m_tabs->removeTab(i);
        table->deleteLater();
    });
    m_tabs->setCurrentIndex(m_tabs->addTab(table, t->name + " table"));
}

void MainWindow::openChecks()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (qobject_cast<ChecksView *>(m_tabs->widget(i))) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    auto *c = new ChecksView(m_world);
    connect(c, &ChecksView::openEntity, this, [this](Id id) { openEntity(id); });
    const int index = m_tabs->addTab(c, "Checks");
    connect(c, &ChecksView::titleChanged, this, [this, c](const QString &t) {
        if (const int i = m_tabs->indexOf(c); i >= 0) m_tabs->setTabText(i, t);
    });
    c->rebuild();
    m_tabs->setCurrentIndex(index);
}

void MainWindow::openHierarchy()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (qobject_cast<HierarchyView *>(m_tabs->widget(i))) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    auto *h = new HierarchyView(m_world);
    connect(h, &HierarchyView::openEntity, this, [this](Id id) { openEntity(id); });
    m_tabs->setCurrentIndex(m_tabs->addTab(h, "Hierarchy"));
}

void MainWindow::openLanes()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (qobject_cast<LanesView *>(m_tabs->widget(i))) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    auto *l = new LanesView(m_world);
    connect(l, &LanesView::openEntity, this, [this](Id id) { openEntity(id); });
    m_tabs->setCurrentIndex(m_tabs->addTab(l, "Timeline"));
}

void MainWindow::openMatrix()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (qobject_cast<MatrixView *>(m_tabs->widget(i))) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    auto *m = new MatrixView(m_world);
    connect(m, &MatrixView::openEntity, this, [this](Id id) { openEntity(id); });
    m_tabs->setCurrentIndex(m_tabs->addTab(m, "Who knows what"));
}

void MainWindow::openGraph()
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (qobject_cast<GraphView *>(m_tabs->widget(i))) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
    auto *g = new GraphView(m_world);
    connect(g, &GraphView::openEntity, this, [this](Id id) { openEntity(id); });
    m_tabs->setCurrentIndex(m_tabs->addTab(g, "Graph"));
    QMetaObject::invokeMethod(g, &GraphView::fitAll, Qt::QueuedConnection);
}

void MainWindow::quickOpen()
{
    if (!m_world->isOpen()) return;
    auto *q = new QuickOpen(m_world, this);
    q->setAttribute(Qt::WA_DeleteOnClose);
    connect(q, &QuickOpen::openEntity, this, &MainWindow::openEntity);
    const QPoint top = mapToGlobal(QPoint((width() - 560) / 2, 90));
    q->move(top);
    q->show();
}

void MainWindow::newItemHere()
{
    // the type of the sheet in front, else the first type
    Id type = 0;
    if (auto *s = qobject_cast<SheetView *>(m_tabs->currentWidget())) {
        if (const auto e = m_world->entity(s->entityId())) type = e->type;
    } else if (auto *t = qobject_cast<TypeEditor *>(m_tabs->currentWidget())) {
        type = t->typeId();
    }
    if (!type && !m_world->types().isEmpty()) type = m_world->types().first().id;
    if (type) m_nav->newEntity(type);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    // a name or value being typed is committed when its box loses focus
    if (QWidget *f = QApplication::focusWidget()) f->clearFocus();
    QSettings().setValue("geometry", saveGeometry());
    e->accept();
}

} // namespace worldsea

#include "moc_mainwindow.cpp"
