#include "core/world.h"

#include "core/starter.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <climits>
#include <exception>

namespace worldsea {

namespace {

constexpr int kSchemaVersion = 1;

// rows these tables hold are the world; everything else (the cursor, graph
// positions) is how it is being looked at, and stays out of undo
constexpr const char *kTracked[] = {"book", "chapter", "etype", "field", "entity", "val", "link", "blob"};

const char *kSchema = R"(
CREATE TABLE IF NOT EXISTS state(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS book(
    id INTEGER PRIMARY KEY, name TEXT NOT NULL, pos INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS chapter(
    id INTEGER PRIMARY KEY,
    book INTEGER NOT NULL REFERENCES book(id) ON DELETE CASCADE,
    title TEXT NOT NULL, pos INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS etype(
    id INTEGER PRIMARY KEY, name TEXT NOT NULL, color TEXT NOT NULL DEFAULT '', pos INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS field(
    id INTEGER PRIMARY KEY,
    etype INTEGER NOT NULL REFERENCES etype(id) ON DELETE CASCADE,
    name TEXT NOT NULL, kind TEXT NOT NULL, pos INTEGER NOT NULL,
    book INTEGER REFERENCES book(id) ON DELETE SET NULL,
    options TEXT NOT NULL DEFAULT '{}',
    inverse INTEGER REFERENCES field(id) ON DELETE SET NULL);
CREATE TABLE IF NOT EXISTS entity(
    id INTEGER PRIMARY KEY,
    etype INTEGER NOT NULL REFERENCES etype(id) ON DELETE CASCADE,
    name TEXT NOT NULL, aliases TEXT NOT NULL DEFAULT '',
    intro INTEGER REFERENCES chapter(id) ON DELETE SET NULL,
    stub INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS val(
    id INTEGER PRIMARY KEY,
    entity INTEGER NOT NULL REFERENCES entity(id) ON DELETE CASCADE,
    field INTEGER NOT NULL REFERENCES field(id) ON DELETE CASCADE,
    chapter INTEGER REFERENCES chapter(id) ON DELETE CASCADE,
    value TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS val_entity ON val(entity, field);
CREATE TABLE IF NOT EXISTS link(
    id INTEGER PRIMARY KEY,
    field INTEGER NOT NULL REFERENCES field(id) ON DELETE CASCADE,
    src INTEGER NOT NULL REFERENCES entity(id) ON DELETE CASCADE,
    dst INTEGER NOT NULL REFERENCES entity(id) ON DELETE CASCADE,
    note TEXT NOT NULL DEFAULT '',
    start_ch INTEGER REFERENCES chapter(id) ON DELETE SET NULL,
    end_ch INTEGER REFERENCES chapter(id) ON DELETE SET NULL);
CREATE INDEX IF NOT EXISTS link_src ON link(src);
CREATE INDEX IF NOT EXISTS link_dst ON link(dst);
CREATE TABLE IF NOT EXISTS blob(id INTEGER PRIMARY KEY, data BLOB NOT NULL);
CREATE TABLE IF NOT EXISTS node_pos(
    entity INTEGER PRIMARY KEY REFERENCES entity(id) ON DELETE CASCADE, x REAL NOT NULL, y REAL NOT NULL);
)";

QString optionsJson(const FieldDef &f)
{
    QJsonObject o;
    if (f.kind == Kind::Ladder) {
        QJsonArray steps;
        for (const auto &s : f.steps) steps.append(QJsonObject{{"id", s.id}, {"name", s.name}});
        o["steps"] = steps;
    }
    if (f.kind == Kind::Reference) {
        QJsonArray targets;
        for (Id t : f.targets) targets.append(t);
        o["targets"] = targets;
        o["multi"] = f.multi;
    }
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void readOptions(FieldDef &f, const QString &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    for (const auto &v : o["steps"].toArray()) f.steps.append({v.toObject().value("id").toInt(), v.toObject().value("name").toString()});
    for (const auto &v : o["targets"].toArray()) f.targets.append(v.toInteger());
    f.multi = o["multi"].toBool(true);
}

QStringList splitAliases(const QString &s)
{
    QStringList out;
    for (const auto &a : s.split('\n')) {
        const QString t = a.trimmed();
        if (!t.isEmpty()) out << t;
    }
    return out;
}

// rewrite pos = index for these rows of a table, in this order
void renumber(Db &db, const char *table, const QList<Id> &order)
{
    Stmt s(db, QStringLiteral("UPDATE %1 SET pos=?1 WHERE id=?2 AND pos<>?1").arg(table).toUtf8().constData());
    for (int i = 0; i < order.size(); ++i) {
        s.bind(1, i).bind(2, order[i]).run();
        s.reset();
    }
}

QList<Id> moved(QList<Id> order, Id id, int pos)
{
    order.removeAll(id);
    order.insert(std::clamp(pos < 0 ? int(order.size()) : pos, 0, int(order.size())), id);
    return order;
}

} // namespace

QString kindKey(Kind k)
{
    switch (k) {
    case Kind::Text: return "text";
    case Kind::LongText: return "longtext";
    case Kind::Number: return "number";
    case Kind::Ladder: return "ladder";
    case Kind::Tags: return "tags";
    case Kind::Image: return "image";
    case Kind::Reference: return "reference";
    }
    return "text";
}

Kind kindFromKey(const QString &key)
{
    for (Kind k : allKinds())
        if (kindKey(k) == key) return k;
    return Kind::Text;
}

QString kindLabel(Kind k)
{
    switch (k) {
    case Kind::Text: return "Text";
    case Kind::LongText: return "Long text";
    case Kind::Number: return "Number";
    case Kind::Ladder: return "Ladder";
    case Kind::Tags: return "Tags";
    case Kind::Image: return "Image";
    case Kind::Reference: return "Links";
    }
    return "Text";
}

QList<Kind> allKinds()
{
    return {Kind::Text, Kind::LongText, Kind::Number, Kind::Ladder, Kind::Tags, Kind::Reference, Kind::Image};
}

World::World(QObject *parent) : QObject(parent) {}

World::~World() { close(); }

void World::open(const QString &path)
{
    close();
    const bool fresh = !QFileInfo::exists(path);
    m_db.open(path);
    m_path = path;
    try {
        m_db.exec("PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;");
        const qint64 version = m_db.scalar("PRAGMA user_version");
        if (version > kSchemaVersion)
            throw DbError(("this world was saved by a newer WorldSea (format " + std::to_string(version) + ")"));
        if (!fresh && version == 0 && m_db.scalar("SELECT count(*) FROM sqlite_master") > 0)
            throw DbError("not a WorldSea world");
        createSchema();
        installUndoTriggers();
        if (fresh || version == 0) {
            {
                Step s(*this, "New world");
                seedStarter();
            }
            // the starter content is where the world begins, not an edit
            m_db.exec("DELETE FROM undolog");
            m_undo.clear();
            saveStacks();
        }
        loadStacks();
    } catch (...) {
        m_db.close();
        m_path.clear();
        throw;
    }

    invalidateTimeline();
    m_mode = loadState("editMode") == "correct" ? EditMode::Correct : EditMode::Change;
    m_cursor = loadState("cursor").toLongLong();
    if (ordinal(m_cursor) < 0) m_cursor = timeline().isEmpty() ? 0 : timeline().first();
    emit changed();
    emit cursorChanged();
    emit editModeChanged();
    emit undoStateChanged();
}

void World::close()
{
    if (!m_db.isOpen()) return;
    m_db.close();
    m_path.clear();
    m_undo.clear();
    m_redo.clear();
    m_depth = 0;
    m_cursor = 0;
    invalidateTimeline();
}

void World::createSchema()
{
    m_db.exec(kSchema);
    m_db.exec(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
}

void World::seedStarter()
{
    const Id book = addBook("Book 1");
    addChapter(book, "Chapter 1");
    starter::addTypes(*this);
    setName(QFileInfo(m_path).completeBaseName());
}

QString World::name() const { return loadState("name"); }

void World::setName(const QString &name) { saveState("name", name); }

void World::saveState(const char *key, const QString &value)
{
    Stmt s(m_db, "INSERT INTO state(key, value) VALUES(?1, ?2) ON CONFLICT(key) DO UPDATE SET value=?2");
    s.bind(1, QString::fromUtf8(key)).bind(2, value).run();
}

QString World::loadState(const char *key) const
{
    Stmt s(m_db, "SELECT value FROM state WHERE key=?1");
    s.bind(1, QString::fromUtf8(key));
    return s.step() ? s.text(0) : QString();
}

// --- undo ------------------------------------------------------------------

void World::installUndoTriggers()
{
    // kept in the file, so undo reaches back past a reopen; the triggers that
    // fill it are per connection
    m_db.exec("CREATE TABLE IF NOT EXISTS undolog(seq INTEGER PRIMARY KEY AUTOINCREMENT, sql TEXT NOT NULL)");
    for (const char *table : kTracked) {
        QStringList cols;
        {
            Stmt s(m_db, QStringLiteral("PRAGMA table_info(%1)").arg(table).toUtf8().constData());
            while (s.step()) cols << s.text(1);
        }
        QStringList sets, names, values;
        for (const auto &c : cols) {
            sets << QStringLiteral("%1='||quote(old.%1)||'").arg(c);
            names << c;
            values << QStringLiteral("'||quote(old.%1)||'").arg(c);
        }
        const QString t = QString::fromUtf8(table);
        m_db.exec(QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS undo_%1_i AFTER INSERT ON main.%1 BEGIN "
                                 "INSERT INTO undolog(sql) VALUES('DELETE FROM %1 WHERE rowid='||new.rowid); END;")
                      .arg(t));
        m_db.exec(QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS undo_%1_u AFTER UPDATE ON main.%1 BEGIN "
                                 "INSERT INTO undolog(sql) VALUES('UPDATE %1 SET %2 WHERE rowid='||old.rowid); END;")
                      .arg(t, sets.join(',')));
        m_db.exec(QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS undo_%1_d BEFORE DELETE ON main.%1 BEGIN "
                                 "INSERT INTO undolog(sql) VALUES('INSERT INTO %1(rowid,%2) VALUES('||old.rowid||',%3)'); END;")
                      .arg(t, names.join(','), values.join(',')));
    }
}

World::Step::Step(World &w, const QString &label) : m_w(w), m_uncaught(std::uncaught_exceptions())
{
    m_w.beginStep(label);
}

World::Step::~Step() { m_w.endStep(std::uncaught_exceptions() == m_uncaught); }

void World::beginStep(const QString &label)
{
    if (m_depth++ > 0) return;
    m_db.exec("BEGIN");
    m_stepStart = m_db.scalar("SELECT COALESCE(MAX(seq), 0) FROM undolog");
    m_stepLabel = label;
}

void World::endStep(bool ok)
{
    if (--m_depth > 0) return;
    if (!ok) {
        try {
            m_db.exec("ROLLBACK");
            // an undo or redo that failed already moved its entry
            loadStacks();
        } catch (...) {
        }
        invalidateTimeline();
        return;
    }
    const qint64 end = m_db.scalar("SELECT COALESCE(MAX(seq), 0) FROM undolog");
    if (end > m_stepStart && !m_replaying) {
        // a new action ends the redo line
        for (const auto &r : m_redo) {
            Stmt d(m_db, "DELETE FROM undolog WHERE seq BETWEEN ?1 AND ?2");
            d.bind(1, r.from).bind(2, r.to).run();
        }
        m_redo.clear();
        m_undo.append({m_stepStart + 1, end, m_stepLabel});
    }
    if (end > m_stepStart) saveStacks();
    m_db.exec("COMMIT");
    invalidateTimeline();
    if (end > m_stepStart) {
        emit changed();
        emit undoStateChanged();
    }
}

void World::saveStacks()
{
    auto encode = [](const QList<UndoEntry> &list) {
        QJsonArray a;
        for (const auto &e : list) a.append(QJsonArray{e.from, e.to, e.label});
        return a;
    };
    const QJsonObject o{{"undo", encode(m_undo)}, {"redo", encode(m_redo)}};
    saveState("undo", QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

void World::loadStacks()
{
    const QJsonObject o = QJsonDocument::fromJson(loadState("undo").toUtf8()).object();
    auto decode = [](const QJsonArray &a) {
        QList<UndoEntry> out;
        for (const auto &v : a) {
            const QJsonArray e = v.toArray();
            out.append({e[0].toInteger(), e[1].toInteger(), e[2].toString()});
        }
        return out;
    };
    m_undo = decode(o["undo"].toArray());
    m_redo = decode(o["redo"].toArray());
}

void World::replay(qint64 from, qint64 to)
{
    QStringList sql;
    {
        Stmt s(m_db, "SELECT sql FROM undolog WHERE seq BETWEEN ?1 AND ?2 ORDER BY seq DESC");
        s.bind(1, from).bind(2, to);
        while (s.step()) sql << s.text(0);
    }
    Stmt d(m_db, "DELETE FROM undolog WHERE seq BETWEEN ?1 AND ?2");
    d.bind(1, from).bind(2, to).run();
    // a cascade logs the parent before its children, so replaying it backwards
    // puts children back first; the keys are checked once it is all back
    m_db.exec("PRAGMA defer_foreign_keys=ON");
    for (const auto &q : sql) m_db.exec(q);
}

void World::undo()
{
    if (m_undo.isEmpty() || m_depth) return;
    const UndoEntry e = m_undo.takeLast();
    m_replaying = true;
    {
        Step s(*this, e.label);
        replay(e.from, e.to);
        const qint64 end = m_db.scalar("SELECT COALESCE(MAX(seq), 0) FROM undolog");
        m_redo.append({m_stepStart + 1, end, e.label});
    }
    m_replaying = false;
    if (m_cursor && ordinal(m_cursor) < 0) setCursor(timeline().isEmpty() ? 0 : timeline().last());
}

void World::redo()
{
    if (m_redo.isEmpty() || m_depth) return;
    const UndoEntry e = m_redo.takeLast();
    m_replaying = true;
    {
        Step s(*this, e.label);
        replay(e.from, e.to);
        const qint64 end = m_db.scalar("SELECT COALESCE(MAX(seq), 0) FROM undolog");
        m_undo.append({m_stepStart + 1, end, e.label});
    }
    m_replaying = false;
    if (m_cursor && ordinal(m_cursor) < 0) setCursor(timeline().isEmpty() ? 0 : timeline().last());
}

// --- books and chapters ----------------------------------------------------

QList<Book> World::books() const
{
    QList<Book> out;
    Stmt s(m_db, "SELECT id, name, pos FROM book ORDER BY pos, id");
    while (s.step()) out.append({s.i64(0), s.text(1), int(s.i64(2))});
    return out;
}

std::optional<Book> World::book(Id id) const
{
    Stmt s(m_db, "SELECT id, name, pos FROM book WHERE id=?1");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return Book{s.i64(0), s.text(1), int(s.i64(2))};
}

Id World::addBook(const QString &name)
{
    Step step(*this, "Add book");
    Stmt s(m_db, "INSERT INTO book(name, pos) VALUES(?1, (SELECT COALESCE(MAX(pos) + 1, 0) FROM book))");
    s.bind(1, name).run();
    return m_db.lastInsertId();
}

void World::renameBook(Id id, const QString &name)
{
    Step step(*this, "Rename book");
    Stmt s(m_db, "UPDATE book SET name=?2 WHERE id=?1 AND name<>?2");
    s.bind(1, id).bind(2, name).run();
}

void World::moveBook(Id id, int pos)
{
    Step step(*this, "Move book");
    QList<Id> order;
    for (const auto &b : books()) order << b.id;
    renumber(m_db, "book", moved(order, id, pos));
}

void World::deleteBook(Id id)
{
    Step step(*this, "Delete book");
    for (const auto &c : chapters(id)) deleteChapter(c.id);
    Stmt s(m_db, "DELETE FROM book WHERE id=?1");
    s.bind(1, id).run();
    QList<Id> order;
    for (const auto &b : books()) order << b.id;
    renumber(m_db, "book", order);
}

QList<Chapter> World::chapters(Id book) const
{
    QList<Chapter> out;
    Stmt s(m_db, "SELECT id, book, title, pos FROM chapter WHERE book=?1 ORDER BY pos, id");
    s.bind(1, book);
    while (s.step()) out.append({s.i64(0), s.i64(1), s.text(2), int(s.i64(3))});
    return out;
}

std::optional<Chapter> World::chapter(Id id) const
{
    Stmt s(m_db, "SELECT id, book, title, pos FROM chapter WHERE id=?1");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return Chapter{s.i64(0), s.i64(1), s.text(2), int(s.i64(3))};
}

Id World::addChapter(Id book, const QString &title, int pos)
{
    Step step(*this, "Add chapter");
    Stmt s(m_db, "INSERT INTO chapter(book, title, pos) VALUES(?1, ?2, (SELECT COALESCE(MAX(pos) + 1, 0) FROM chapter WHERE book=?1))");
    s.bind(1, book).bind(2, title).run();
    const Id id = m_db.lastInsertId();
    if (pos >= 0) moveChapter(id, book, pos);
    invalidateTimeline();
    if (!m_cursor) setCursor(id);
    return id;
}

void World::renameChapter(Id id, const QString &title)
{
    Step step(*this, "Rename chapter");
    Stmt s(m_db, "UPDATE chapter SET title=?2 WHERE id=?1 AND title<>?2");
    s.bind(1, id).bind(2, title).run();
}

void World::fixChapterPositions(Id book)
{
    QList<Id> order;
    for (const auto &c : chapters(book)) order << c.id;
    renumber(m_db, "chapter", order);
}

void World::moveChapter(Id id, Id book, int pos)
{
    const auto c = chapter(id);
    if (!c) return;
    Step step(*this, "Move chapter");
    if (c->book != book) {
        Stmt s(m_db, "UPDATE chapter SET book=?2, pos=-1 WHERE id=?1");
        s.bind(1, id).bind(2, book).run();
        fixChapterPositions(c->book);
    }
    QList<Id> order;
    for (const auto &ch : chapters(book)) order << ch.id;
    renumber(m_db, "chapter", moved(order, id, pos));
    invalidateTimeline();
}

Id World::chapterBefore(Id chapter) const
{
    const int o = ordinal(chapter);
    return o > 0 ? timeline()[o - 1] : 0;
}

void World::reassignChapter(Id from, Id to)
{
    // a value recorded in both: the later one (the one moving) wins
    Stmt drop(m_db, "DELETE FROM val WHERE chapter IS ?2 AND EXISTS "
                    "(SELECT 1 FROM val v WHERE v.chapter=?1 AND v.entity=val.entity AND v.field=val.field)");
    drop.bind(1, from).bindId(2, to).run();
    Stmt v(m_db, "UPDATE val SET chapter=?2 WHERE chapter=?1");
    v.bind(1, from).bindId(2, to).run();
    Stmt ls(m_db, "UPDATE link SET start_ch=?2 WHERE start_ch=?1");
    ls.bind(1, from).bindId(2, to).run();
    Stmt le(m_db, "UPDATE link SET end_ch=?2 WHERE end_ch=?1");
    le.bind(1, from).bindId(2, to).run();
    // a link that started and ended in the same chapter never held
    Stmt empty(m_db, "DELETE FROM link WHERE end_ch IS ?1 AND start_ch IS ?1 AND end_ch IS NOT NULL");
    empty.bindId(1, to).run();
    Stmt e(m_db, "UPDATE entity SET intro=?2 WHERE intro=?1");
    e.bind(1, from).bindId(2, to).run();
}

void World::deleteChapter(Id id)
{
    const auto c = chapter(id);
    if (!c) return;
    Step step(*this, "Delete chapter");
    const Id before = chapterBefore(id);
    reassignChapter(id, before);
    Stmt s(m_db, "DELETE FROM chapter WHERE id=?1");
    s.bind(1, id).run();
    fixChapterPositions(c->book);
    invalidateTimeline();
    if (m_cursor == id) {
        Id next = before;
        if (!next && !timeline().isEmpty()) next = timeline().first();
        setCursor(next);
    }
}

int World::recordedIn(Id chapter) const
{
    Stmt s(m_db, "SELECT (SELECT count(*) FROM val WHERE chapter=?1) + (SELECT count(*) FROM link WHERE start_ch=?1)"
                 " + (SELECT count(*) FROM link WHERE end_ch=?1) + (SELECT count(*) FROM entity WHERE intro=?1)");
    s.bind(1, chapter);
    return s.step() ? int(s.i64(0)) : 0;
}

void World::invalidateTimeline() { m_timelineValid = false; }

const QList<Id> &World::timeline() const
{
    if (!m_timelineValid) {
        m_timeline.clear();
        m_ordinal.clear();
        if (m_db.isOpen()) {
            Stmt s(m_db, "SELECT c.id FROM chapter c JOIN book b ON b.id=c.book ORDER BY b.pos, b.id, c.pos, c.id");
            while (s.step()) {
                m_ordinal.insert(s.i64(0), int(m_timeline.size()));
                m_timeline << s.i64(0);
            }
        }
        m_timelineValid = true;
    }
    return m_timeline;
}

int World::ordinal(Id chapter) const
{
    timeline();
    return chapter ? m_ordinal.value(chapter, -1) : -1;
}

QString World::chapterLabel(Id ch) const
{
    const auto c = chapter(ch);
    if (!c) return QStringLiteral("the start");
    const auto b = book(c->book);
    return (b ? b->name : QString()) + QStringLiteral(" · ") + c->title;
}

void World::setCursor(Id chapter)
{
    if (chapter && ordinal(chapter) < 0) return;
    if (chapter == m_cursor) return;
    m_cursor = chapter;
    saveState("cursor", QString::number(chapter));
    emit cursorChanged();
}

void World::setEditMode(EditMode m)
{
    if (m == m_mode) return;
    m_mode = m;
    saveState("editMode", m == EditMode::Correct ? "correct" : "change");
    emit editModeChanged();
}

Id World::cursorBook() const
{
    const auto c = chapter(m_cursor);
    return c ? c->book : 0;
}

// --- types and fields -----------------------------------------------------

QList<EntityType> World::types() const
{
    QList<EntityType> out;
    Stmt s(m_db, "SELECT id, name, color, pos FROM etype ORDER BY pos, id");
    while (s.step()) out.append({s.i64(0), s.text(1), s.text(2), int(s.i64(3))});
    return out;
}

std::optional<EntityType> World::type(Id id) const
{
    Stmt s(m_db, "SELECT id, name, color, pos FROM etype WHERE id=?1");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return EntityType{s.i64(0), s.text(1), s.text(2), int(s.i64(3))};
}

Id World::addType(const QString &name, const QString &color)
{
    Step step(*this, "Add type");
    Stmt s(m_db, "INSERT INTO etype(name, color, pos) VALUES(?1, ?2, (SELECT COALESCE(MAX(pos) + 1, 0) FROM etype))");
    s.bind(1, name).bind(2, color).run();
    return m_db.lastInsertId();
}

void World::renameType(Id id, const QString &name)
{
    Step step(*this, "Rename type");
    Stmt s(m_db, "UPDATE etype SET name=?2 WHERE id=?1 AND name<>?2");
    s.bind(1, id).bind(2, name).run();
}

void World::setTypeColor(Id id, const QString &color)
{
    Step step(*this, "Type color");
    Stmt s(m_db, "UPDATE etype SET color=?2 WHERE id=?1 AND color<>?2");
    s.bind(1, id).bind(2, color).run();
}

void World::moveType(Id id, int pos)
{
    Step step(*this, "Move type");
    QList<Id> order;
    for (const auto &t : types()) order << t.id;
    renumber(m_db, "etype", moved(order, id, pos));
}

void World::deleteType(Id id)
{
    Step step(*this, "Delete type");
    // reference fields elsewhere that only pointed here lose the target
    for (const auto &t : types()) {
        for (auto f : fields(t.id)) {
            if (f.kind == Kind::Reference && f.targets.removeAll(id)) updateField(f);
        }
    }
    Stmt s(m_db, "DELETE FROM etype WHERE id=?1");
    s.bind(1, id).run();
}

FieldDef World::readField(Stmt &s) const
{
    FieldDef f;
    f.id = s.i64(0);
    f.type = s.i64(1);
    f.name = s.text(2);
    f.kind = kindFromKey(s.text(3));
    f.pos = int(s.i64(4));
    f.book = s.i64(5);
    readOptions(f, s.text(6));
    f.inverse = s.i64(7);
    return f;
}

QList<FieldDef> World::fields(Id type, Id book) const
{
    QList<FieldDef> out;
    Stmt s(m_db, "SELECT id, etype, name, kind, pos, book, options, inverse FROM field WHERE etype=?1"
                 " AND (?2 = -1 OR book IS NULL OR book=?2) ORDER BY pos, id");
    s.bind(1, type).bind(2, book);
    while (s.step()) out << readField(s);
    return out;
}

std::optional<FieldDef> World::field(Id id) const
{
    Stmt s(m_db, "SELECT id, etype, name, kind, pos, book, options, inverse FROM field WHERE id=?1");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return readField(s);
}

Id World::addField(Id type, const QString &name, Kind kind)
{
    Step step(*this, "Add field");
    FieldDef f;
    f.kind = kind;
    Stmt s(m_db, "INSERT INTO field(etype, name, kind, pos, options) VALUES(?1, ?2, ?3,"
                 " (SELECT COALESCE(MAX(pos) + 1, 0) FROM field WHERE etype=?1), ?4)");
    s.bind(1, type).bind(2, name).bind(3, kindKey(kind)).bind(4, optionsJson(f)).run();
    return m_db.lastInsertId();
}

void World::updateField(const FieldDef &f)
{
    const auto old = field(f.id);
    if (!old) return;
    Step step(*this, "Edit field");
    Stmt s(m_db, "UPDATE field SET name=?2, kind=?3, book=?4, options=?5 WHERE id=?1");
    s.bind(1, f.id).bind(2, f.name).bind(3, kindKey(f.kind)).bindId(4, f.book).bind(5, optionsJson(f)).run();
    // no longer a reference: its links and its pairing go
    if (old->kind == Kind::Reference && f.kind != Kind::Reference) {
        setInverse(f.id, {});
        Stmt d(m_db, "DELETE FROM link WHERE field=?1");
        d.bind(1, f.id).run();
    }
}

void World::moveField(Id id, int pos)
{
    const auto f = field(id);
    if (!f) return;
    Step step(*this, "Move field");
    QList<Id> order;
    for (const auto &x : fields(f->type)) order << x.id;
    renumber(m_db, "field", moved(order, id, pos));
}

void World::deleteField(Id id)
{
    const auto f = field(id);
    if (!f) return;
    Step step(*this, "Delete field");
    Stmt s(m_db, "DELETE FROM field WHERE id=?1");
    s.bind(1, id).run();
    QList<Id> order;
    for (const auto &x : fields(f->type)) order << x.id;
    renumber(m_db, "field", order);
}

Id World::setInverse(Id fieldId, const QString &inverseName)
{
    const auto f = field(fieldId);
    if (!f || f->kind != Kind::Reference) return 0;
    Step step(*this, "Pair fields");
    const QString name = inverseName.trimmed();
    // unpair from the current partner first
    if (f->inverse) {
        Stmt u(m_db, "UPDATE field SET inverse=NULL WHERE id=?1 AND inverse=?2");
        u.bind(1, f->inverse).bind(2, fieldId).run();
        Stmt v(m_db, "UPDATE field SET inverse=NULL WHERE id=?1");
        v.bind(1, fieldId).run();
    }
    if (name.isEmpty()) return 0;

    const Id target = f->targets.isEmpty() ? f->type : f->targets.first();
    Id partner = 0;
    if (target == f->type && name.compare(f->name, Qt::CaseInsensitive) == 0) {
        partner = fieldId;
    } else {
        for (const auto &x : fields(target)) {
            if (x.kind == Kind::Reference && x.name.compare(name, Qt::CaseInsensitive) == 0) partner = x.id;
        }
        if (!partner) {
            partner = addField(target, name, Kind::Reference);
            FieldDef p = *field(partner);
            p.targets = {f->type};
            p.book = f->book;
            updateField(p);
        }
        const auto p = field(partner);
        // the partner leaves its own previous pairing
        if (p->inverse && p->inverse != fieldId) {
            Stmt u(m_db, "UPDATE field SET inverse=NULL WHERE id=?1 AND inverse=?2");
            u.bind(1, p->inverse).bind(2, partner).run();
        }
        Stmt u(m_db, "UPDATE field SET inverse=?2 WHERE id=?1");
        u.bind(1, partner).bind(2, fieldId).run();
    }
    Stmt u(m_db, "UPDATE field SET inverse=?2 WHERE id=?1");
    u.bind(1, fieldId).bind(2, partner).run();
    return partner;
}

// --- items -------------------------------------------------------------------

Entity World::readEntity(Stmt &s) const
{
    Entity e;
    e.id = s.i64(0);
    e.type = s.i64(1);
    e.name = s.text(2);
    e.aliases = splitAliases(s.text(3));
    e.intro = s.i64(4);
    e.stub = s.i64(5) != 0;
    return e;
}

QList<Entity> World::entities(Id type) const
{
    QList<Entity> out;
    Stmt s(m_db, "SELECT id, etype, name, aliases, intro, stub FROM entity WHERE ?1 = 0 OR etype=?1 ORDER BY name COLLATE NOCASE, id");
    s.bind(1, type);
    while (s.step()) out << readEntity(s);
    return out;
}

std::optional<Entity> World::entity(Id id) const
{
    Stmt s(m_db, "SELECT id, etype, name, aliases, intro, stub FROM entity WHERE id=?1");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return readEntity(s);
}

Id World::addEntity(Id type, const QString &name, bool stub)
{
    Step step(*this, "Add item");
    Stmt s(m_db, "INSERT INTO entity(etype, name, intro, stub) VALUES(?1, ?2, ?3, ?4)");
    s.bind(1, type).bind(2, name).bindId(3, m_mode == EditMode::Change ? m_cursor : 0).bind(4, stub ? 1 : 0).run();
    return m_db.lastInsertId();
}

void World::renameEntity(Id id, const QString &name)
{
    Step step(*this, "Rename item");
    Stmt s(m_db, "UPDATE entity SET name=?2 WHERE id=?1 AND name<>?2");
    s.bind(1, id).bind(2, name).run();
}

void World::setAliases(Id id, const QStringList &aliases)
{
    Step step(*this, "Aliases");
    QStringList clean;
    for (const auto &a : aliases)
        if (!a.trimmed().isEmpty()) clean << a.trimmed();
    Stmt s(m_db, "UPDATE entity SET aliases=?2 WHERE id=?1 AND aliases<>?2");
    s.bind(1, id).bind(2, clean.join('\n')).run();
}

void World::setIntro(Id id, Id chapter)
{
    Step step(*this, "First appearance");
    Stmt s(m_db, "UPDATE entity SET intro=?2 WHERE id=?1 AND intro IS NOT ?2");
    s.bind(1, id).bindId(2, chapter).run();
}

void World::setStub(Id id, bool stub)
{
    Step step(*this, stub ? "Mark as placeholder" : "Fill in placeholder");
    Stmt s(m_db, "UPDATE entity SET stub=?2 WHERE id=?1 AND stub<>?2");
    s.bind(1, id).bind(2, stub ? 1 : 0).run();
}

void World::deleteEntity(Id id)
{
    Step step(*this, "Delete item");
    Stmt s(m_db, "DELETE FROM entity WHERE id=?1");
    s.bind(1, id).run();
}

std::optional<Entity> World::findEntity(const QString &name, const QList<Id> &types) const
{
    const QString n = name.trimmed();
    if (n.isEmpty()) return std::nullopt;
    std::optional<Entity> byAlias;
    for (const auto &e : entities()) {
        if (!types.isEmpty() && !types.contains(e.type)) continue;
        if (e.name.compare(n, Qt::CaseInsensitive) == 0) return e;
        if (!byAlias) {
            for (const auto &a : e.aliases)
                if (a.compare(n, Qt::CaseInsensitive) == 0) byAlias = e;
        }
    }
    return byAlias;
}

bool World::existsAt(const Entity &e, Id chapter) const { return ordinal(e.intro) <= ordinal(chapter); }

// --- values -----------------------------------------------------------------

QList<ValueRow> World::valueRows(Id entity, Id field) const
{
    QList<ValueRow> out;
    Stmt s(m_db, "SELECT id, entity, field, chapter, value FROM val WHERE entity=?1 AND field=?2");
    s.bind(1, entity).bind(2, field);
    while (s.step()) out.append({s.i64(0), s.i64(1), s.i64(2), s.i64(3), s.text(4)});
    std::stable_sort(out.begin(), out.end(), [&](const ValueRow &a, const ValueRow &b) { return ordinal(a.chapter) < ordinal(b.chapter); });
    return out;
}

Effective World::effective(Id entity, Id field, Id chapter) const
{
    Effective e;
    const int at = ordinal(chapter);
    int best = INT_MIN;
    int next = INT_MAX;
    for (const auto &r : valueRows(entity, field)) {
        const int o = ordinal(r.chapter);
        if (r.chapter && o < 0) continue; // a chapter that no longer exists
        if (o <= at && o > best) {
            best = o;
            e.has = true;
            e.value = r.value;
            e.rowId = r.id;
            e.since = r.chapter;
        } else if (o > at && o < next) {
            next = o;
            e.nextChange = r.chapter;
        }
    }
    return e;
}

void World::setValue(Id entity, Id field, const QString &value, EditMode mode)
{
    Step step(*this, "Edit value");
    if (mode == EditMode::Correct || !m_cursor) {
        const Effective cur = effective(entity, field, m_cursor);
        if (cur.has) {
            if (value.isEmpty() && !cur.since && valueRows(entity, field).size() == 1) {
                deleteValueRow(cur.rowId);
                return;
            }
            Stmt s(m_db, "UPDATE val SET value=?2 WHERE id=?1 AND value<>?2");
            s.bind(1, cur.rowId).bind(2, value).run();
        } else if (!value.isEmpty()) {
            Stmt s(m_db, "INSERT INTO val(entity, field, chapter, value) VALUES(?1, ?2, NULL, ?3)");
            s.bind(1, entity).bind(2, field).bind(3, value).run();
        }
        return;
    }
    // what it was before this chapter (for the first chapter, the start)
    const Effective before = effective(entity, field, chapterBefore(m_cursor));
    Id here = 0;
    for (const auto &r : valueRows(entity, field))
        if (r.chapter == m_cursor) here = r.id;
    if (here) {
        if (before.has ? value == before.value : value.isEmpty()) {
            deleteValueRow(here);
        } else {
            Stmt s(m_db, "UPDATE val SET value=?2 WHERE id=?1 AND value<>?2");
            s.bind(1, here).bind(2, value).run();
        }
        return;
    }
    const Effective cur = effective(entity, field, m_cursor);
    if (cur.has ? cur.value == value : value.isEmpty()) return;
    Stmt s(m_db, "INSERT INTO val(entity, field, chapter, value) VALUES(?1, ?2, ?3, ?4)");
    s.bind(1, entity).bind(2, field).bind(3, m_cursor).bind(4, value).run();
}

void World::deleteValueRow(Id rowId)
{
    Step step(*this, "Delete value");
    Stmt s(m_db, "DELETE FROM val WHERE id=?1");
    s.bind(1, rowId).run();
}

void World::moveValueRow(Id rowId, Id chapter)
{
    Step step(*this, "Move change");
    // the row already recorded there gives way
    Stmt d(m_db, "DELETE FROM val WHERE chapter IS ?2 AND id<>?1 AND EXISTS"
                 " (SELECT 1 FROM val v WHERE v.id=?1 AND v.entity=val.entity AND v.field=val.field)");
    d.bind(1, rowId).bindId(2, chapter).run();
    Stmt s(m_db, "UPDATE val SET chapter=?2 WHERE id=?1");
    s.bind(1, rowId).bindId(2, chapter).run();
}

// --- links ------------------------------------------------------------------

Link World::readLink(Stmt &s) const
{
    return Link{s.i64(0), s.i64(1), s.i64(2), s.i64(3), s.text(4), s.i64(5), s.i64(6)};
}

bool World::linkActiveAt(const Link &l, Id chapter) const
{
    const int at = ordinal(chapter);
    const int start = ordinal(l.start);
    const int end = l.end ? ordinal(l.end) : INT_MAX;
    if (l.end && end < 0) return start <= at; // its end chapter is gone
    return start <= at && at < end;
}

QList<LinkView> World::links(Id entity, Id fieldId, std::optional<Id> at) const
{
    QList<LinkView> out;
    const auto f = field(fieldId);
    if (!f) return out;
    Stmt s(m_db, "SELECT id, field, src, dst, note, start_ch, end_ch FROM link"
                 " WHERE (field=?2 AND src=?1) OR (field=?3 AND dst=?1) ORDER BY id");
    s.bind(1, entity).bind(2, fieldId).bindId(3, f->inverse);
    while (s.step()) {
        const Link l = readLink(s);
        if (at && !linkActiveAt(l, *at)) continue;
        // a symmetric field stores each link once, on whichever side made it
        const bool outgoing = l.field == fieldId && l.src == entity;
        out.append({l, outgoing ? l.dst : l.src, outgoing});
    }
    return out;
}

QList<Link> World::allLinks(std::optional<Id> at) const
{
    QList<Link> out;
    Stmt s(m_db, "SELECT id, field, src, dst, note, start_ch, end_ch FROM link ORDER BY id");
    while (s.step()) {
        const Link l = readLink(s);
        if (!at || linkActiveAt(l, *at)) out << l;
    }
    return out;
}

std::optional<Link> World::link(Id id) const
{
    Stmt s(m_db, "SELECT id, field, src, dst, note, start_ch, end_ch FROM link WHERE id=?1");
    s.bind(1, id);
    if (!s.step()) return std::nullopt;
    return readLink(s);
}

Id World::addLink(Id fieldId, Id src, Id dst, const QString &note)
{
    const auto f = field(fieldId);
    if (!f || !src || !dst) return 0;
    Step step(*this, "Link");
    const bool change = m_mode == EditMode::Change && m_cursor;
    const auto current = links(src, fieldId, m_cursor);
    for (const auto &v : current)
        if (v.other == dst) return v.link.id;
    if (!f->multi && !current.isEmpty()) {
        const Link &old = current.first().link;
        if (!change || old.start == m_cursor) {
            // repoint the link in place
            Stmt s(m_db, "UPDATE link SET field=?2, src=?3, dst=?4, note=?5 WHERE id=?1");
            s.bind(1, old.id).bind(2, fieldId).bind(3, src).bind(4, dst).bind(5, note).run();
            for (int i = 1; i < current.size(); ++i) removeLink(current[i].link.id);
            return old.id;
        }
        for (const auto &v : current) removeLink(v.link.id);
    }
    Stmt s(m_db, "INSERT INTO link(field, src, dst, note, start_ch) VALUES(?1, ?2, ?3, ?4, ?5)");
    s.bind(1, fieldId).bind(2, src).bind(3, dst).bind(4, note).bindId(5, change ? m_cursor : 0).run();
    return m_db.lastInsertId();
}

void World::removeLink(Id linkId)
{
    const auto l = link(linkId);
    if (!l) return;
    Step step(*this, "Unlink");
    const bool change = m_mode == EditMode::Change && m_cursor;
    if (!change || ordinal(m_cursor) <= ordinal(l->start)) {
        deleteLinkRow(linkId);
        return;
    }
    Stmt s(m_db, "UPDATE link SET end_ch=?2 WHERE id=?1");
    s.bind(1, linkId).bind(2, m_cursor).run();
}

void World::setLinkNote(Id linkId, const QString &note)
{
    Step step(*this, "Link note");
    Stmt s(m_db, "UPDATE link SET note=?2 WHERE id=?1 AND note<>?2");
    s.bind(1, linkId).bind(2, note).run();
}

void World::setLinkSpan(Id linkId, Id start, Id end)
{
    Step step(*this, "Link chapters");
    Stmt s(m_db, "UPDATE link SET start_ch=?2, end_ch=?3 WHERE id=?1");
    s.bind(1, linkId).bindId(2, start).bindId(3, end).run();
}

void World::deleteLinkRow(Id linkId)
{
    Step step(*this, "Delete link");
    Stmt s(m_db, "DELETE FROM link WHERE id=?1");
    s.bind(1, linkId).run();
}

QList<HistoryEntry> World::history(Id entity) const
{
    QList<HistoryEntry> out;
    if (const auto e = this->entity(entity); e && e->intro) out.append({HistoryEntry::Intro, e->intro, 0, {}, 0, 0});
    {
        Stmt s(m_db, "SELECT id, field, chapter, value FROM val WHERE entity=?1 ORDER BY id");
        s.bind(1, entity);
        while (s.step()) out.append({HistoryEntry::Value, s.i64(2), s.i64(1), s.text(3), 0, s.i64(0)});
    }
    {
        Stmt s(m_db, "SELECT id, field, src, dst, note, start_ch, end_ch FROM link WHERE src=?1 OR dst=?1 ORDER BY id");
        s.bind(1, entity);
        while (s.step()) {
            const Link l = readLink(s);
            const bool outgoing = l.src == entity;
            // seen from the other side, it is the inverse field that changed
            Id f = l.field;
            if (!outgoing) {
                const auto fd = field(l.field);
                f = fd && fd->inverse ? fd->inverse : l.field;
            }
            const Id other = outgoing ? l.dst : l.src;
            out.append({HistoryEntry::LinkStart, l.start, f, l.note, other, l.id});
            if (l.end) out.append({HistoryEntry::LinkEnd, l.end, f, {}, other, l.id});
        }
    }
    std::stable_sort(out.begin(), out.end(), [&](const HistoryEntry &a, const HistoryEntry &b) { return ordinal(a.chapter) < ordinal(b.chapter); });
    return out;
}

// --- images ----------------------------------------------------------------

Id World::addBlob(const QByteArray &data)
{
    Step step(*this, "Add image");
    Stmt s(m_db, "INSERT INTO blob(data) VALUES(?1)");
    s.bind(1, data).run();
    return m_db.lastInsertId();
}

QByteArray World::blob(Id id) const
{
    Stmt s(m_db, "SELECT data FROM blob WHERE id=?1");
    s.bind(1, id);
    return s.step() ? s.blob(0) : QByteArray();
}

// --- graph positions -------------------------------------------------------

QHash<Id, QPointF> World::nodePositions() const
{
    QHash<Id, QPointF> out;
    Stmt s(m_db, "SELECT entity, x, y FROM node_pos");
    while (s.step()) out.insert(s.i64(0), QPointF(s.num(1), s.num(2)));
    return out;
}

void World::setNodePosition(Id entity, QPointF p)
{
    Stmt s(m_db, "INSERT INTO node_pos(entity, x, y) VALUES(?1, ?2, ?3) ON CONFLICT(entity) DO UPDATE SET x=?2, y=?3");
    s.bind(1, entity).bind(2, p.x()).bind(3, p.y()).run();
}

} // namespace worldsea

#include "moc_world.cpp"
