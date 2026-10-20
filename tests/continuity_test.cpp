#include "core/continuity.h"
#include "core/world.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

using namespace worldsea;

namespace {

struct ContinuityTest : ::testing::Test {
    QTemporaryDir dir;
    World w;
    Id book = 0, ch1 = 0, ch2 = 0, ch3 = 0, character = 0, event = 0;

    void SetUp() override
    {
        w.open(dir.filePath("t.worldsea"));
        book = w.books().first().id;
        ch1 = w.chapters(book).first().id;
        ch2 = w.addChapter(book, "Chapter 2");
        ch3 = w.addChapter(book, "Chapter 3");
        character = w.types()[0].id;
        event = w.types()[3].id;
    }
    Id field(Id type, const QString &name)
    {
        for (const auto &f : w.fields(type))
            if (f.name == name) return f.id;
        return 0;
    }
    bool has(const QString &needle)
    {
        for (const auto &i : checkContinuity(w))
            if (i.text.contains(needle)) return true;
        return false;
    }
};

} // namespace

TEST_F(ContinuityTest, CleanWorldHasNoIssues)
{
    w.setCursor(ch1);
    const Id a = w.addEntity(character, "Lin");
    w.setValue(a, field(character, "Status"), "Alive");
    EXPECT_TRUE(checkContinuity(w).isEmpty());
}

TEST_F(ContinuityTest, DeadCharacterInALaterEvent)
{
    EXPECT_TRUE(readsAsDead("Killed by Elder Mo"));
    EXPECT_FALSE(readsAsDead("Deadly calm"));
    w.setCursor(ch1);
    const Id lin = w.addEntity(character, "Lin");
    w.setCursor(ch2);
    w.setValue(lin, field(character, "Status"), "Dead");
    w.setCursor(ch3);
    const Id battle = w.addEntity(event, "Battle");
    w.addLink(field(event, "Participants"), battle, lin);
    EXPECT_TRUE(has("Lin is dead since"));
}

TEST_F(ContinuityTest, LinkedBeforeAppearingAndLadderGoingDown)
{
    w.setCursor(ch2);
    const Id late = w.addEntity(character, "Late");
    w.setCursor(ch1);
    const Id lin = w.addEntity(character, "Lin");
    w.addLink(field(character, "Allies"), lin, late);
    EXPECT_TRUE(has("Late is linked (Allies)"));

    const auto cult = *w.field(field(character, "Cultivation"));
    w.setValue(lin, cult.id, QString::number(cult.steps[2].id));
    w.setCursor(ch3);
    w.setValue(lin, cult.id, QString::number(cult.steps[1].id));
    EXPECT_TRUE(has("goes down from Foundation Establishment to Qi Condensation"));
}

TEST_F(ContinuityTest, SingleValuedFieldHoldingTwo)
{
    w.setCursor(ch1);
    const Id lin = w.addEntity(character, "Lin");
    const Id mo = w.addEntity(character, "Mo");
    const Id hua = w.addEntity(character, "Hua");
    const Id master = field(character, "Master");
    const Id first = w.addLink(master, lin, mo);
    w.setCursor(ch2);
    w.addLink(master, lin, hua); // ends the first in ch2
    EXPECT_FALSE(has("two Master at once"));
    w.setLinkSpan(first, 0, 0); // ...until someone says it never ended
    EXPECT_TRUE(has("two Master at once"));
}
