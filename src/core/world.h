#pragma once
// A world: one SQLite file holding books and their chapters, item types and
// their sheets, the items, and every value and link along the story's
// timeline.
//
// Time is the chapter. Each value row says which chapter it becomes true in
// (none = from the start), and reading an item "as of" a chapter takes, per
// field, the row from the latest chapter at or before it. Links carry the
// chapter they start in and, once ended, the chapter they end in. Chapters
// are referenced by id, so reordering them never rewrites history.
//
// A reference field is the relation: linking Disciples on one character to
// another is the link. A field may name an inverse field on its target type
// (Disciples <-> Master, or a field that is its own inverse like Allies), and
// a link stored on one side reads back on the other.
//
// Every change goes through a Step, one undo entry. Undo uses SQLite's undo
// log idiom: temp triggers write the inverse statement of every row change,
// and undoing a step replays its inverses in reverse.

#include "core/sqlite.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>

namespace worldsea {

using Id = qint64;

enum class Kind { Text, LongText, Number, Ladder, Tags, Image, Reference };
QString kindKey(Kind k);
Kind kindFromKey(const QString &key);
QString kindLabel(Kind k);
QList<Kind> allKinds();

// How an edit lands on the timeline: fix the value in place, or record that
// it changed in the cursor's chapter and keep the old one.
enum class EditMode { Correct, Change };

struct Book {
    Id id = 0;
    QString name;
    int pos = 0;
};

struct Chapter {
    Id id = 0;
    Id book = 0;
    QString title;
    int pos = 0;
};

struct LadderStep {
    int id = 0;
    QString name;
};

struct EntityType {
    Id id = 0;
    QString name;
    QString color;
    int pos = 0;
};

struct FieldDef {
    Id id = 0;
    Id type = 0;
    QString name;
    Kind kind = Kind::Text;
    int pos = 0;
    Id book = 0; // 0: every book; else only shown while the cursor is in it
    QList<LadderStep> steps; // Ladder, lowest first
    QList<Id> targets;       // Reference: the types it may point at (empty = any)
    bool multi = true;       // Reference: more than one link at a time
    Id inverse = 0;          // Reference: the paired field on the other side
};

struct Entity {
    Id id = 0;
    Id type = 0;
    QString name;
    QStringList aliases;
    Id intro = 0; // the chapter it first appears in, 0 = from the start
    bool stub = false;
};

struct ValueRow {
    Id id = 0;
    Id entity = 0;
    Id field = 0;
    Id chapter = 0;
    QString value;
};

struct Link {
    Id id = 0;
    Id field = 0;
    Id src = 0;
    Id dst = 0;
    QString note;
    Id start = 0; // 0 = from the start
    Id end = 0;   // 0 = still holds
};

// a link as seen from one of its two items
struct LinkView {
    Link link;
    Id other = 0;
    bool outgoing = true; // stored on this side's field, not the inverse
};

// the value of one field as of a chapter
struct Effective {
    bool has = false;
    QString value;
    Id rowId = 0;
    Id since = 0;      // chapter of the row in effect (0 = from the start)
    Id nextChange = 0; // the first later chapter that changes it, 0 = none
};

// one entry in an item's history
struct HistoryEntry {
    enum What { Value, LinkStart, LinkEnd, Intro } what = Value;
    Id chapter = 0;
    Id field = 0;
    QString value; // Value: the new value
    Id other = 0;  // links: the other item
    Id rowId = 0;  // the val or link row
};

class World : public QObject {
    Q_OBJECT
public:
    explicit World(QObject *parent = nullptr);
    ~World() override;

    // creates the file with the starter types when it does not exist yet
    void open(const QString &path);
    void close();
    bool isOpen() const { return m_db.isOpen(); }
    QString path() const { return m_path; }
    QString name() const;
    void setName(const QString &name);

    // --- undo -------------------------------------------------------------
    // One user action. Nested steps fold into the outermost; the step commits
    // and becomes one undo entry when the outermost one ends.
    class Step {
    public:
        Step(World &w, const QString &label);
        ~Step();
        Step(const Step &) = delete;
        Step &operator=(const Step &) = delete;

    private:
        World &m_w;
        int m_uncaught;
    };
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString undoLabel() const { return m_undo.isEmpty() ? QString() : m_undo.last().label; }
    QString redoLabel() const { return m_redo.isEmpty() ? QString() : m_redo.last().label; }
    void undo();
    void redo();

    // --- books and chapters ----------------------------------------------
    QList<Book> books() const;
    std::optional<Book> book(Id id) const;
    Id addBook(const QString &name);
    void renameBook(Id id, const QString &name);
    void moveBook(Id id, int pos);
    // its chapters go too; what was recorded in them moves to the chapter before
    void deleteBook(Id id);

    QList<Chapter> chapters(Id book) const;
    std::optional<Chapter> chapter(Id id) const;
    // appended at the end, or inserted at pos
    Id addChapter(Id book, const QString &title, int pos = -1);
    void renameChapter(Id id, const QString &title);
    void moveChapter(Id id, Id book, int pos);
    // what was recorded in it moves to the chapter before (or the start)
    void deleteChapter(Id id);
    // how many values, link starts/ends and introductions sit in a chapter
    int recordedIn(Id chapter) const;

    // every chapter of every book in reading order
    const QList<Id> &timeline() const;
    // position on the timeline; -1 for 0 (the start) or an unknown chapter
    int ordinal(Id chapter) const;
    // "Book · Chapter", or "the start"
    QString chapterLabel(Id chapter) const;

    // the chapter the world is being looked at as of; 0 = no chapters
    Id cursor() const { return m_cursor; }
    void setCursor(Id chapter);
    EditMode editMode() const { return m_mode; }
    void setEditMode(EditMode m);
    // the book the cursor is in
    Id cursorBook() const;

    // --- types and fields -------------------------------------------------
    QList<EntityType> types() const;
    std::optional<EntityType> type(Id id) const;
    Id addType(const QString &name, const QString &color = {});
    void renameType(Id id, const QString &name);
    void setTypeColor(Id id, const QString &color);
    void moveType(Id id, int pos);
    // its items and fields go too
    void deleteType(Id id);

    // a type's fields for a book (world-wide ones plus that book's), in order;
    // book = -1 lists every field regardless of book
    QList<FieldDef> fields(Id type, Id book = -1) const;
    std::optional<FieldDef> field(Id id) const;
    Id addField(Id type, const QString &name, Kind kind);
    // name, kind, book and options; position and inverse have their own calls
    void updateField(const FieldDef &f);
    void moveField(Id id, int pos);
    void deleteField(Id id);
    // pair a reference field with a field on its (first) target type, creating
    // it by that name when the type has none; "" unpairs. A field whose target
    // is its own type may name itself, which makes it symmetric.
    Id setInverse(Id field, const QString &inverseName);

    // --- items ------------------------------------------------------------
    QList<Entity> entities(Id type = 0) const;
    std::optional<Entity> entity(Id id) const;
    // in Change mode it is introduced at the cursor's chapter
    Id addEntity(Id type, const QString &name, bool stub = false);
    void renameEntity(Id id, const QString &name);
    void setAliases(Id id, const QStringList &aliases);
    void setIntro(Id id, Id chapter);
    void setStub(Id id, bool stub);
    void deleteEntity(Id id);
    // case-insensitive by name or alias, optionally of one of these types
    std::optional<Entity> findEntity(const QString &name, const QList<Id> &types = {}) const;
    // introduced at or before the chapter
    bool existsAt(const Entity &e, Id chapter) const;

    // --- values -----------------------------------------------------------
    Effective effective(Id entity, Id field, Id chapter) const;
    Effective effective(Id entity, Id field) const { return effective(entity, field, m_cursor); }
    // in Correct mode the row in effect is rewritten (a new one from the start
    // when there is none); in Change mode the value is recorded at the cursor,
    // and a change back to what it already was removes the record
    void setValue(Id entity, Id field, const QString &value, EditMode mode);
    void setValue(Id entity, Id field, const QString &value) { setValue(entity, field, value, m_mode); }
    void deleteValueRow(Id rowId);
    void moveValueRow(Id rowId, Id chapter);
    QList<ValueRow> valueRows(Id entity, Id field) const;

    // --- links ------------------------------------------------------------
    // a field's links for an item, both the ones stored on it and the ones
    // stored on its inverse, that hold as of the chapter (nullopt: every link
    // ever made)
    QList<LinkView> links(Id entity, Id field, std::optional<Id> at) const;
    QList<LinkView> links(Id entity, Id field) const { return links(entity, field, m_cursor); }
    QList<Link> allLinks(std::optional<Id> at) const;
    std::optional<Link> link(Id id) const;
    // chapter 0 is the start
    bool linkActiveAt(const Link &l, Id chapter) const;
    // Change mode starts it at the cursor; a single-valued field ends (Change)
    // or repoints (Correct) the link it already has
    Id addLink(Id field, Id src, Id dst, const QString &note = {});
    // Correct deletes it; Change ends it at the cursor
    void removeLink(Id linkId);
    void setLinkNote(Id linkId, const QString &note);
    void setLinkSpan(Id linkId, Id start, Id end);
    void deleteLinkRow(Id linkId);

    // everything recorded about an item, in timeline order
    QList<HistoryEntry> history(Id entity) const;

    // --- images -------------------------------------------------------------
    Id addBlob(const QByteArray &data);
    QByteArray blob(Id id) const;

    // --- graph positions (not part of undo) --------------------------------
    QHash<Id, QPointF> nodePositions() const;
    void setNodePosition(Id entity, QPointF p);

signals:
    // anything in the file changed (an edit, an undo, a redo)
    void changed();
    void cursorChanged();
    void editModeChanged();
    void undoStateChanged();

private:
    friend class Step;
    void createSchema();
    void seedStarter();
    void installUndoTriggers();
    void beginStep(const QString &label);
    void endStep(bool ok);
    void replay(qint64 from, qint64 to);
    // the undo and redo lines, kept in the file beside the log they point into
    void saveStacks();
    void loadStacks();
    void invalidateTimeline();
    void saveState(const char *key, const QString &value);
    QString loadState(const char *key) const;
    // the chapter immediately before this one on the timeline, 0 if first
    Id chapterBefore(Id chapter) const;
    void reassignChapter(Id from, Id to);
    void fixChapterPositions(Id book);
    FieldDef readField(Stmt &s) const;
    Entity readEntity(Stmt &s) const;
    Link readLink(Stmt &s) const;

    mutable Db m_db;
    QString m_path;
    Id m_cursor = 0;
    EditMode m_mode = EditMode::Change;

    struct UndoEntry {
        qint64 from = 0, to = 0; // undolog seq range
        QString label;
    };
    QList<UndoEntry> m_undo, m_redo;
    int m_depth = 0;
    qint64 m_stepStart = 0;
    QString m_stepLabel;
    bool m_replaying = false;

    mutable QList<Id> m_timeline;
    mutable QHash<Id, int> m_ordinal;
    mutable bool m_timelineValid = false;
};

} // namespace worldsea
