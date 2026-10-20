#include "core/wikiexport.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

namespace worldsea {

namespace {

const char *kCss = R"(body { background: #191919; color: #dddddd; font: 15px/1.55 system-ui, sans-serif; margin: 0; }
main { max-width: 760px; margin: 0 auto; padding: 36px 20px 60px; }
a { color: #c9a86a; text-decoration: none; } a:hover { text-decoration: underline; }
h1 { font-weight: 500; font-size: 30px; margin: 8px 0 4px; }
h2 { color: #8a8a8a; font-size: 12px; letter-spacing: 1px; font-weight: 500; margin: 30px 0 8px; text-transform: uppercase; }
.muted { color: #8a8a8a; } .type { font-size: 13px; }
table { border-collapse: collapse; width: 100%; } td { padding: 7px 0; border-bottom: 1px solid #262626; vertical-align: top; }
td.k { color: #8a8a8a; width: 170px; padding-right: 16px; }
ul { padding-left: 18px; } li { margin: 3px 0; }
img { max-width: 260px; border-radius: 6px; }
.chip { display: inline-block; border: 1px solid #3a3a3a; border-radius: 12px; padding: 1px 10px; margin: 0 4px 4px 0; }
)";

QString esc(const QString &s) { return s.toHtmlEscaped(); }

QString page(const QString &title, const QString &body)
{
    return QStringLiteral("<!doctype html>\n<html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
                          "content=\"width=device-width, initial-scale=1\"><title>%1</title>"
                          "<link rel=\"stylesheet\" href=\"style.css\"></head><body><main>\n%2</main></body></html>\n")
        .arg(esc(title), body);
}

bool write(const QString &path, const QByteArray &data, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
        if (error) *error = path + ": " + f.errorString();
        return false;
    }
    return true;
}

QString itemFile(Id id) { return QStringLiteral("item-%1.html").arg(id); }

} // namespace

bool spoilerTypeByDefault(const QString &typeName)
{
    return typeName.compare("Secret", Qt::CaseInsensitive) == 0 || typeName.compare("Plot thread", Qt::CaseInsensitive) == 0;
}

int exportWiki(const World &w, const WikiExport &opt, QString *error)
{
    if (!QDir().mkpath(opt.dir)) {
        if (error) *error = "cannot create " + opt.dir;
        return -1;
    }
    const QDir dir(opt.dir);
    const int at = w.ordinal(opt.asOf);
    const QString asOf = opt.asOf ? w.chapterLabel(opt.asOf) : QStringLiteral("the start");

    // who readers have met by then
    QHash<Id, Entity> shown;
    for (const auto &e : w.entities())
        if (opt.types.contains(e.type) && w.existsAt(e, opt.asOf)) shown.insert(e.id, e);
    QHash<Id, EntityType> types;
    for (const auto &t : w.types()) types.insert(t.id, t);
    auto linkTo = [&](Id id) {
        return QStringLiteral("<a class=\"chip\" href=\"%1\">%2</a>").arg(itemFile(id), esc(shown[id].name));
    };

    if (!write(dir.filePath("style.css"), kCss, error)) return -1;
    int pages = 0;
    QDir().mkpath(dir.filePath("images"));

    for (const auto &e : shown) {
        QString body;
        body += QStringLiteral("<p><a href=\"index.html\">← %1</a></p>\n").arg(esc(w.name()));
        body += QStringLiteral("<div class=\"type\" style=\"color:%1\">● %2</div>\n<h1>%3</h1>\n")
                    .arg(types[e.type].color.isEmpty() ? "#c9a86a" : types[e.type].color, esc(types[e.type].name), esc(e.name));
        if (!e.aliases.isEmpty()) body += QStringLiteral("<p class=\"muted\">Also known as %1</p>\n").arg(esc(e.aliases.join(", ")));

        QString rows;
        for (const auto &f : w.fields(e.type, w.chapter(opt.asOf) ? w.chapter(opt.asOf)->book : -1)) {
            QString value;
            if (f.kind == Kind::Reference) {
                for (const auto &v : w.links(e.id, f.id, opt.asOf))
                    if (shown.contains(v.other)) value += linkTo(v.other);
            } else {
                const Effective eff = w.effective(e.id, f.id, opt.asOf);
                if (!eff.has || eff.value.isEmpty()) continue;
                if (f.kind == Kind::Image) {
                    const QByteArray img = w.blob(eff.value.toLongLong());
                    const QString name = QStringLiteral("images/%1.%2").arg(eff.value, img.startsWith("\x89PNG") ? "png" : "jpg");
                    if (!write(dir.filePath(name), img, error)) return -1;
                    value = QStringLiteral("<img src=\"%1\" alt=\"\">").arg(name);
                } else if (f.kind == Kind::LongText) {
                    for (const auto &para : eff.value.split('\n', Qt::SkipEmptyParts)) value += "<p>" + esc(para) + "</p>";
                } else if (f.kind == Kind::Chapter) {
                    // a chapter readers have not reached yet would give it away
                    if (w.ordinal(eff.value.toLongLong()) > at) continue;
                    value = esc(displayValue(w, f, eff.value));
                } else {
                    value = esc(displayValue(w, f, eff.value));
                }
            }
            if (!value.isEmpty()) rows += QStringLiteral("<tr><td class=\"k\">%1</td><td>%2</td></tr>\n").arg(esc(f.name), value);
        }
        if (!rows.isEmpty()) body += "<table>\n" + rows + "</table>\n";

        QString history;
        Id last = -1;
        for (const auto &h : w.history(e.id)) {
            if (w.ordinal(h.chapter) > at || !h.chapter) continue;
            const auto f = w.field(h.field);
            QString line;
            switch (h.what) {
            case HistoryEntry::Intro: line = "First appears"; break;
            case HistoryEntry::Value:
                if (!f || f->kind == Kind::Image || (f->kind == Kind::Chapter && w.ordinal(h.value.toLongLong()) > at)) continue;
                line = esc(f->name) + " → " + esc(displayValue(w, *f, h.value));
                break;
            case HistoryEntry::LinkStart:
            case HistoryEntry::LinkEnd:
                if (!f || !shown.contains(h.other)) continue;
                line = esc(f->name) + (h.what == HistoryEntry::LinkStart ? " + " : " − ") + linkTo(h.other);
                break;
            }
            if (h.chapter != last) {
                if (last != -1) history += "</ul>\n";
                history += QStringLiteral("<p class=\"muted\">%1</p><ul>\n").arg(esc(w.chapterLabel(h.chapter)));
                last = h.chapter;
            }
            history += "<li>" + line + "</li>\n";
        }
        if (!history.isEmpty()) body += "<h2>So far</h2>\n" + history + "</ul>\n";
        if (!write(dir.filePath(itemFile(e.id)), page(e.name, body).toUtf8(), error)) return -1;
        ++pages;
    }

    QString index = QStringLiteral("<h1>%1</h1>\n<p class=\"muted\">As of %2. Nothing past it is here.</p>\n").arg(esc(w.name()), esc(asOf));
    for (const auto &t : w.types()) {
        if (!opt.types.contains(t.id)) continue;
        QStringList links;
        for (const auto &e : w.entities(t.id))
            if (shown.contains(e.id)) links << QStringLiteral("<li><a href=\"%1\">%2</a></li>").arg(itemFile(e.id), esc(e.name));
        if (links.isEmpty()) continue;
        index += QStringLiteral("<h2>%1</h2>\n<ul>\n%2\n</ul>\n").arg(esc(t.name), links.join('\n'));
    }
    if (!write(dir.filePath("index.html"), page(w.name(), index).toUtf8(), error)) return -1;
    return pages + 1;
}

} // namespace worldsea
