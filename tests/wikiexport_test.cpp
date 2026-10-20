#include "core/wikiexport.h"
#include "core/world.h"

#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace worldsea;

namespace {
QString read(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(f.readAll());
}
} // namespace

TEST(WikiExport, NothingPastTheChapterGetsIn)
{
    QTemporaryDir dir;
    World w;
    w.open(dir.filePath("w.worldsea"));
    const Id book = w.books().first().id;
    const Id ch1 = w.chapters(book).first().id;
    const Id ch2 = w.addChapter(book, "Chapter 2");
    const Id character = w.types()[0].id;
    Id status = 0;
    for (const auto &f : w.fields(character))
        if (f.name == "Status") status = f.id;

    w.setCursor(ch1);
    const Id lin = w.addEntity(character, "Lin Feng");
    w.setValue(lin, status, "Outer disciple");
    w.setCursor(ch2);
    w.setValue(lin, status, "Sect heir");
    const Id late = w.addEntity(character, "Late Arrival");
    Id allies = 0;
    for (const auto &f : w.fields(character))
        if (f.name == "Allies") allies = f.id;
    w.addLink(allies, lin, late);

    WikiExport opt;
    opt.asOf = ch1;
    for (const auto &t : w.types())
        if (!spoilerTypeByDefault(t.name)) opt.types.insert(t.id);
    opt.dir = dir.filePath("wiki");
    QString error;
    ASSERT_EQ(exportWiki(w, opt, &error), 2) << error.toStdString(); // Lin's page and the index
    const QString page = read(dir.filePath(QStringLiteral("wiki/item-%1.html").arg(lin)));
    EXPECT_TRUE(page.contains("Outer disciple"));
    EXPECT_FALSE(page.contains("Sect heir"));
    EXPECT_FALSE(page.contains("Late Arrival"));
    EXPECT_FALSE(read(dir.filePath("wiki/index.html")).contains("Late Arrival"));
}
