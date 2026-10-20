#include "core/search.h"
#include "core/world.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace worldsea;

namespace {

struct WorldTest : ::testing::Test {
    QTemporaryDir dir;
    World w;
    Id book = 0, ch1 = 0, ch2 = 0, ch3 = 0;
    Id character = 0;

    void SetUp() override
    {
        w.open(dir.filePath("test.worldsea"));
        book = w.books().first().id;
        ch1 = w.chapters(book).first().id;
        ch2 = w.addChapter(book, "Chapter 2");
        ch3 = w.addChapter(book, "Chapter 3");
        character = w.types().first().id;
    }

    Id fieldNamed(Id type, const QString &name)
    {
        for (const auto &f : w.fields(type))
            if (f.name == name) return f.id;
        return 0;
    }
};

} // namespace

TEST_F(WorldTest, StarterWorldHasTypesAndNoUndo)
{
    EXPECT_EQ(w.types().size(), 6);
    w.undo(); // the two chapters SetUp added
    w.undo();
    EXPECT_FALSE(w.canUndo());
    EXPECT_EQ(w.timeline().size(), 1);
    const auto master = w.field(fieldNamed(character, "Master"));
    ASSERT_TRUE(master);
    const auto disciples = w.field(master->inverse);
    ASSERT_TRUE(disciples);
    EXPECT_EQ(disciples->name, "Disciples");
    EXPECT_EQ(disciples->inverse, master->id);
    const auto allies = w.field(fieldNamed(character, "Allies"));
    EXPECT_EQ(allies->inverse, allies->id);
}

TEST_F(WorldTest, ValuesResolveAsOfChapter)
{
    const Id lin = w.addEntity(character, "Lin Feng");
    const Id status = fieldNamed(character, "Status");
    w.setCursor(ch1);
    w.setValue(lin, status, "Alive", EditMode::Change);
    w.setCursor(ch3);
    w.setValue(lin, status, "Dead", EditMode::Change);

    EXPECT_EQ(w.effective(lin, status, ch1).value, "Alive");
    EXPECT_EQ(w.effective(lin, status, ch2).value, "Alive");
    EXPECT_EQ(w.effective(lin, status, ch2).nextChange, ch3);
    EXPECT_EQ(w.effective(lin, status, ch3).value, "Dead");
    EXPECT_EQ(w.effective(lin, status, ch3).since, ch3);

    // a correction rewrites the value in effect, not a new one
    w.setCursor(ch2);
    w.setValue(lin, status, "Wounded", EditMode::Correct);
    EXPECT_EQ(w.effective(lin, status, ch1).value, "Wounded");
    EXPECT_EQ(w.valueRows(lin, status).size(), 2);

    // changing back to what it was removes the record
    w.setCursor(ch3);
    w.setValue(lin, status, "Wounded", EditMode::Change);
    EXPECT_EQ(w.valueRows(lin, status).size(), 1);
}

TEST_F(WorldTest, ReorderingChaptersKeepsHistoryByChapter)
{
    const Id lin = w.addEntity(character, "Lin Feng");
    const Id status = fieldNamed(character, "Status");
    w.setCursor(ch2);
    w.setValue(lin, status, "Exiled", EditMode::Change);
    w.moveChapter(ch2, book, 2); // now ch1, ch3, ch2
    EXPECT_EQ(w.timeline(), (QList<Id>{ch1, ch3, ch2}));
    EXPECT_FALSE(w.effective(lin, status, ch3).has);
    EXPECT_EQ(w.effective(lin, status, ch2).value, "Exiled");
}

TEST_F(WorldTest, DeletingAChapterMovesItsRecordsBack)
{
    const Id lin = w.addEntity(character, "Lin Feng");
    const Id status = fieldNamed(character, "Status");
    w.setCursor(ch1);
    w.setValue(lin, status, "Alive", EditMode::Change);
    w.setCursor(ch2);
    w.setValue(lin, status, "Exiled", EditMode::Change);
    EXPECT_EQ(w.recordedIn(ch2), 1);
    w.deleteChapter(ch2);
    EXPECT_EQ(w.cursor(), ch1);
    EXPECT_EQ(w.effective(lin, status, ch1).value, "Exiled");
    EXPECT_EQ(w.valueRows(lin, status).size(), 1);
}

TEST_F(WorldTest, LinksReadFromBothSidesAndEndInChapters)
{
    const Id lin = w.addEntity(character, "Lin Feng");
    const Id mo = w.addEntity(character, "Elder Mo");
    const Id master = fieldNamed(character, "Master");
    const Id disciples = w.field(master)->inverse;

    w.setCursor(ch1);
    w.addLink(disciples, mo, lin, "first disciple");
    ASSERT_EQ(w.links(lin, master, ch1).size(), 1);
    EXPECT_EQ(w.links(lin, master, ch1).first().other, mo);
    EXPECT_FALSE(w.links(lin, master, ch1).first().outgoing);

    w.setCursor(ch3);
    w.removeLink(w.links(lin, master, ch3).first().link.id);
    EXPECT_EQ(w.links(lin, master, ch2).size(), 1);
    EXPECT_TRUE(w.links(lin, master, ch3).isEmpty());
    EXPECT_EQ(w.links(lin, master, std::nullopt).size(), 1);

    // a symmetric field shows on both items
    const Id allies = fieldNamed(character, "Allies");
    w.addLink(allies, lin, mo);
    EXPECT_EQ(w.links(mo, allies, ch3).size(), 1);
    EXPECT_EQ(w.links(lin, allies, ch3).size(), 1);
}

TEST_F(WorldTest, SingleValuedFieldEndsThePreviousLink)
{
    const Id lin = w.addEntity(character, "Lin Feng");
    const Id mo = w.addEntity(character, "Elder Mo");
    const Id hua = w.addEntity(character, "Sect Master Hua");
    const Id master = fieldNamed(character, "Master");
    w.setCursor(ch1);
    w.addLink(master, lin, mo);
    w.setCursor(ch2);
    w.addLink(master, lin, hua);
    EXPECT_EQ(w.links(lin, master, ch1).first().other, mo);
    ASSERT_EQ(w.links(lin, master, ch2).size(), 1);
    EXPECT_EQ(w.links(lin, master, ch2).first().other, hua);
    int linkEntries = 0;
    for (const auto &h : w.history(lin)) linkEntries += h.what != HistoryEntry::Intro;
    EXPECT_EQ(linkEntries, 3); // two starts, one end
}

TEST_F(WorldTest, UndoAndRedoRestoreCascades)
{
    const Id lin = w.addEntity(character, "Lin Feng");
    const Id mo = w.addEntity(character, "Elder Mo");
    const Id allies = fieldNamed(character, "Allies");
    const Id status = fieldNamed(character, "Status");
    w.addLink(allies, lin, mo);
    w.setValue(lin, status, "Alive");

    w.deleteType(character);
    EXPECT_FALSE(w.entity(lin));
    w.undo();
    ASSERT_TRUE(w.entity(lin));
    EXPECT_EQ(w.links(lin, allies, std::nullopt).size(), 1);
    EXPECT_EQ(w.effective(lin, status).value, "Alive");
    EXPECT_EQ(w.field(allies)->inverse, allies);

    w.redo();
    EXPECT_FALSE(w.entity(lin));
    w.undo();
    w.undo(); // the value
    EXPECT_FALSE(w.effective(lin, status).has);
    w.redo();
    EXPECT_EQ(w.effective(lin, status).value, "Alive");
}

TEST_F(WorldTest, IntroducedInChapter)
{
    w.setCursor(ch2);
    w.setEditMode(EditMode::Change);
    const Id e = w.addEntity(character, "Late Arrival");
    EXPECT_FALSE(w.existsAt(*w.entity(e), ch1));
    EXPECT_TRUE(w.existsAt(*w.entity(e), ch3));
    w.setEditMode(EditMode::Correct);
    const Id f = w.addEntity(character, "Always There");
    EXPECT_TRUE(w.existsAt(*w.entity(f), ch1));
}

TEST_F(WorldTest, InversePairingCreatesTheOtherField)
{
    const Id org = w.types()[1].id;
    const Id rivals = w.addField(org, "Rival of", Kind::Reference);
    FieldDef f = *w.field(rivals);
    f.targets = {character};
    w.updateField(f);
    const Id other = w.setInverse(rivals, "Rival sects");
    ASSERT_TRUE(other);
    EXPECT_EQ(w.field(other)->type, character);
    EXPECT_EQ(w.field(other)->targets, QList<Id>{org});
    w.setInverse(rivals, {});
    EXPECT_FALSE(w.field(other)->inverse);
}

TEST_F(WorldTest, FindAndSearchUseAliases)
{
    const Id lin = w.addEntity(character, "Lin Feng");
    w.setAliases(lin, {"Young Master Lin", "Azure Demon"});
    EXPECT_EQ(w.findEntity("azure demon")->id, lin);
    const auto hits = searchEntities(w, "azdem");
    ASSERT_FALSE(hits.isEmpty());
    EXPECT_EQ(hits.first().entity.id, lin);
    EXPECT_EQ(hits.first().matched, "Azure Demon");
    EXPECT_LT(fuzzyScore("xyz", "Lin Feng"), 0);
}

TEST_F(WorldTest, UndoSurvivesReopening)
{
    const Id lin = w.addEntity(character, "Lin Feng");
    w.renameEntity(lin, "Lin Fan");
    const QString path = w.path();
    w.close();
    World again;
    again.open(path);
    ASSERT_TRUE(again.canUndo());
    EXPECT_EQ(again.undoLabel(), "Rename item");
    again.undo();
    EXPECT_EQ(again.entity(lin)->name, "Lin Feng");
    again.undo();
    EXPECT_FALSE(again.entity(lin));
    again.redo();
    EXPECT_EQ(again.entity(lin)->name, "Lin Feng");
}

TEST_F(WorldTest, ReopenKeepsCursorAndMode)
{
    w.setCursor(ch2);
    w.setEditMode(EditMode::Correct);
    const QString path = w.path();
    w.close();
    World again;
    again.open(path);
    EXPECT_EQ(again.cursor(), ch2);
    EXPECT_EQ(again.editMode(), EditMode::Correct);
    EXPECT_EQ(again.timeline().size(), 3);
}
