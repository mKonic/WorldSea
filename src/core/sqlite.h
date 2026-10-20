#pragma once
// A thin layer over the SQLite C API: one connection, prepared statements
// with positional binds, and errors that name the statement that failed.

#include <QByteArray>
#include <QString>
#include <QVariant>

#include <stdexcept>

struct sqlite3;
struct sqlite3_stmt;

namespace worldsea {

struct DbError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Db;

class Stmt {
public:
    Stmt(Db &db, const char *sql);
    ~Stmt();
    Stmt(const Stmt &) = delete;
    Stmt &operator=(const Stmt &) = delete;

    // positional, from 1; a null QVariant (or an id of 0 through bindId) binds NULL
    Stmt &bind(int i, const QVariant &v);
    Stmt &bindId(int i, qint64 id);
    // returns true while there is a row; throws on error
    bool step();
    // runs to completion
    void run();
    void reset();

    qint64 i64(int col) const;
    double num(int col) const;
    QString text(int col) const;
    QByteArray blob(int col) const;
    bool isNull(int col) const;

private:
    Db &m_db;
    sqlite3_stmt *m_st = nullptr;
    QByteArray m_sql;
};

class Db {
public:
    Db() = default;
    ~Db();
    Db(const Db &) = delete;
    Db &operator=(const Db &) = delete;

    void open(const QString &path);
    void close();
    bool isOpen() const { return m_db != nullptr; }

    // one or more statements, no results
    void exec(const char *sql);
    void exec(const QString &sql) { exec(sql.toUtf8().constData()); }
    qint64 lastInsertId() const;
    // a single value, or the fallback when there is no row
    qint64 scalar(const char *sql, qint64 fallback = 0);

    sqlite3 *raw() { return m_db; }
    QString lastError() const;

private:
    sqlite3 *m_db = nullptr;
};

} // namespace worldsea
