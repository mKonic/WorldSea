#include "core/continuity.h"

#include <QRegularExpression>

#include <algorithm>
#include <climits>

namespace worldsea {

bool readsAsDead(const QString &status)
{
    static const QRegularExpression dead(QStringLiteral("\\b(dead|deceased|died|killed|slain|perished)\\b"),
                                         QRegularExpression::CaseInsensitiveOption);
    return dead.match(status).hasMatch();
}

QList<Issue> checkContinuity(const World &w)
{
    QList<Issue> out;
    QHash<Id, Entity> entities;
    for (const auto &e : w.entities()) entities.insert(e.id, e);
    QHash<Id, FieldDef> fields;
    for (const auto &t : w.types())
        for (const auto &f : w.fields(t.id)) fields.insert(f.id, f);
    auto name = [&](Id e) { return entities.value(e).name; };
    // with one book, the chapter's title says enough
    const bool oneBook = w.books().size() < 2;
    auto label = [&](Id ch) {
        const auto c = w.chapter(ch);
        return oneBook && c ? c->title : w.chapterLabel(ch);
    };

    // when each item dies, by a Status-like field reading as dead
    QHash<Id, Id> diedIn;
    for (const auto &e : entities) {
        for (const auto &f : fields) {
            if (f.type != e.type || f.kind != Kind::Text || f.name.compare("Status", Qt::CaseInsensitive) != 0) continue;
            for (const auto &r : w.valueRows(e.id, f.id)) {
                if (readsAsDead(r.value)) {
                    diedIn.insert(e.id, r.chapter);
                    break;
                }
            }
        }
    }

    for (const auto &e : entities) {
        const int intro = w.ordinal(e.intro);
        if (e.stub)
            out.append({Issue::Note, e.id, e.intro, 0, QStringLiteral("%1 is a placeholder with nothing filled in").arg(e.name)});

        for (const auto &f : fields) {
            if (f.type != e.type || f.kind == Kind::Reference) continue;
            const auto rows = w.valueRows(e.id, f.id); // timeline order
            for (const auto &r : rows) {
                if (r.chapter && w.ordinal(r.chapter) < intro)
                    out.append({Issue::Warning, e.id, r.chapter, f.id,
                                QStringLiteral("%1's %2 is recorded in %3, before %1 first appears (%4)")
                                    .arg(e.name, f.name, label(r.chapter), label(e.intro))});
            }
            if (f.kind == Kind::Ladder) {
                auto rank = [&](const QString &v) {
                    for (int i = 0; i < f.steps.size(); ++i)
                        if (QString::number(f.steps[i].id) == v) return i;
                    return -1;
                };
                for (int i = 1; i < rows.size(); ++i) {
                    const int a = rank(rows[i - 1].value), b = rank(rows[i].value);
                    if (a >= 0 && b >= 0 && b < a)
                        out.append({Issue::Note, e.id, rows[i].chapter, f.id,
                                    QStringLiteral("%1's %2 goes down from %3 to %4 in %5")
                                        .arg(e.name, f.name, f.steps[a].name, f.steps[b].name, label(rows[i].chapter))});
                }
            }
        }
    }

    // chapter fields read in their sheet order: a setup, then its payoff
    for (const auto &t : w.types()) {
        QList<FieldDef> marks;
        for (const auto &f : w.fields(t.id))
            if (f.kind == Kind::Chapter) marks << f;
        if (marks.size() < 2) continue;
        for (const auto &e : entities) {
            if (e.type != t.id) continue;
            QList<std::pair<FieldDef, Id>> set;
            for (const auto &f : marks) {
                const Effective v = w.effective(e.id, f.id, w.timeline().isEmpty() ? 0 : w.timeline().last());
                if (v.has && !v.value.isEmpty()) set.append({f, v.value.toLongLong()});
            }
            for (int i = 1; i < set.size(); ++i) {
                if (w.ordinal(set[i].second) < w.ordinal(set[i - 1].second))
                    out.append({Issue::Warning, e.id, set[i].second, set[i].first.id,
                                QStringLiteral("%1: %2 (%3) comes before %4 (%5)")
                                    .arg(e.name, set[i].first.name, label(set[i].second), set[i - 1].first.name, label(set[i - 1].second))});
            }
            if (!set.isEmpty() && set.last().first.id != marks.last().id)
                out.append({Issue::Note, e.id, set.last().second, marks.last().id,
                            QStringLiteral("%1 is still open: %2 %3, no %4 yet")
                                .arg(e.name, set.last().first.name, label(set.last().second), marks.last().name)});
        }
    }

    // links: spans, appearances, deaths, single-valued fields holding two at once
    const auto links = w.allLinks(std::nullopt);
    QHash<std::pair<Id, Id>, QList<Link>> single; // (field, src) of single-valued fields
    for (const auto &l : links) {
        const FieldDef f = fields.value(l.field);
        const int start = w.ordinal(l.start);
        if (l.end && w.ordinal(l.end) >= 0 && w.ordinal(l.end) <= start)
            out.append({Issue::Warning, l.src, l.end, l.field,
                        QStringLiteral("%1 → %2 (%3) ends in %4, no later than it starts")
                            .arg(name(l.src), name(l.dst), f.name, label(l.end))});
        for (Id side : {l.src, l.dst}) {
            const Entity &e = entities[side];
            if (l.start && start < w.ordinal(e.intro))
                out.append({Issue::Warning, side, l.start, l.field,
                            QStringLiteral("%1 is linked (%2) in %3, before first appearing in %4")
                                .arg(e.name, f.name, label(l.start), label(e.intro))});
            if (diedIn.contains(side) && l.start && start > w.ordinal(diedIn[side]))
                out.append({Issue::Warning, side, l.start, l.field,
                            QStringLiteral("%1 is dead since %2 but gains a link (%3, %4) in %5")
                                .arg(e.name, label(diedIn[side]), f.name, name(side == l.src ? l.dst : l.src), label(l.start))});
        }
        if (!f.multi) single[{l.field, l.src}] << l;
    }
    for (auto it = single.begin(); it != single.end(); ++it) {
        const auto &ls = it.value();
        for (int i = 0; i < ls.size(); ++i) {
            for (int j = i + 1; j < ls.size(); ++j) {
                const Link &a = ls[i], &b = ls[j];
                auto span = [&](const Link &l) {
                    return std::pair{w.ordinal(l.start), l.end ? w.ordinal(l.end) : INT_MAX};
                };
                const auto [as, ae] = span(a);
                const auto [bs, be] = span(b);
                if (std::max(as, bs) < std::min(ae, be)) {
                    const Id at = as >= bs ? a.start : b.start;
                    out.append({Issue::Warning, a.src, at, a.field,
                                QStringLiteral("%1 has two %2 at once in %3: %4 and %5")
                                    .arg(name(a.src), fields.value(a.field).name, label(at), name(a.dst), name(b.dst))});
                }
            }
        }
    }

    std::stable_sort(out.begin(), out.end(), [&](const Issue &a, const Issue &b) {
        if (a.severity != b.severity) return a.severity < b.severity;
        return w.ordinal(a.chapter) < w.ordinal(b.chapter);
    });
    return out;
}

} // namespace worldsea
