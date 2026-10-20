#include "core/sqlite.h"

#include <sqlite3.h>

namespace worldsea {

Stmt::Stmt(Db &db, const char *sql) : m_db(db), m_sql(sql)
{
    if (sqlite3_prepare_v2(db.raw(), sql, -1, &m_st, nullptr) != SQLITE_OK)
        throw DbError(QStringLiteral("prepare: %1 in %2").arg(db.lastError(), sql).toStdString());
}

Stmt::~Stmt() { sqlite3_finalize(m_st); }

Stmt &Stmt::bind(int i, const QVariant &v)
{
    int rc = SQLITE_OK;
    if (v.isNull()) {
        rc = sqlite3_bind_null(m_st, i);
    } else {
        switch (v.typeId()) {
        case QMetaType::Int:
        case QMetaType::LongLong:
        case QMetaType::UInt:
        case QMetaType::ULongLong:
        case QMetaType::Bool:
            rc = sqlite3_bind_int64(m_st, i, v.toLongLong());
            break;
        case QMetaType::Double:
            rc = sqlite3_bind_double(m_st, i, v.toDouble());
            break;
        case QMetaType::QByteArray: {
            const QByteArray b = v.toByteArray();
            rc = sqlite3_bind_blob(m_st, i, b.constData(), int(b.size()), SQLITE_TRANSIENT);
            break;
        }
        default: {
            const QByteArray s = v.toString().toUtf8();
            rc = sqlite3_bind_text(m_st, i, s.constData(), int(s.size()), SQLITE_TRANSIENT);
        }
        }
    }
    if (rc != SQLITE_OK) throw DbError(("bind: " + m_db.lastError() + " in " + m_sql).toStdString());
    return *this;
}

Stmt &Stmt::bindId(int i, qint64 id) { return bind(i, id ? QVariant(id) : QVariant()); }

bool Stmt::step()
{
    const int rc = sqlite3_step(m_st);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    const QString err = m_db.lastError();
    sqlite3_reset(m_st);
    throw DbError(("step: " + err + " in " + m_sql).toStdString());
}

void Stmt::run()
{
    while (step()) {
    }
}

void Stmt::reset()
{
    sqlite3_reset(m_st);
    sqlite3_clear_bindings(m_st);
}

qint64 Stmt::i64(int col) const { return sqlite3_column_int64(m_st, col); }
double Stmt::num(int col) const { return sqlite3_column_double(m_st, col); }
bool Stmt::isNull(int col) const { return sqlite3_column_type(m_st, col) == SQLITE_NULL; }

QString Stmt::text(int col) const
{
    const auto *p = reinterpret_cast<const char *>(sqlite3_column_text(m_st, col));
    return p ? QString::fromUtf8(p, sqlite3_column_bytes(m_st, col)) : QString();
}

QByteArray Stmt::blob(int col) const
{
    const void *p = sqlite3_column_blob(m_st, col);
    return p ? QByteArray(static_cast<const char *>(p), sqlite3_column_bytes(m_st, col)) : QByteArray();
}

Db::~Db() { close(); }

void Db::open(const QString &path)
{
    close();
    if (sqlite3_open_v2(path.toUtf8().constData(), &m_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        const QString err = lastError();
        close();
        throw DbError(("open " + path + ": " + err).toStdString());
    }
    sqlite3_busy_timeout(m_db, 2000);
}

void Db::close()
{
    if (m_db) sqlite3_close_v2(m_db);
    m_db = nullptr;
}

void Db::exec(const char *sql)
{
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        const QString msg = QString::fromUtf8(err ? err : "unknown");
        sqlite3_free(err);
        throw DbError(QStringLiteral("exec: %1 in %2").arg(msg, QString::fromUtf8(sql).left(200)).toStdString());
    }
}

qint64 Db::lastInsertId() const { return sqlite3_last_insert_rowid(m_db); }

qint64 Db::scalar(const char *sql, qint64 fallback)
{
    Stmt s(*this, sql);
    return s.step() ? s.i64(0) : fallback;
}

QString Db::lastError() const { return m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QStringLiteral("no database"); }

} // namespace worldsea
