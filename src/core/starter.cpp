#include "core/starter.h"

#include "core/world.h"

namespace worldsea::starter {

namespace {

Id plain(World &w, Id type, const QString &name, Kind kind)
{
    return w.addField(type, name, kind);
}

Id ladder(World &w, Id type, const QString &name, const QStringList &steps)
{
    const Id id = w.addField(type, name, Kind::Ladder);
    FieldDef f = *w.field(id);
    for (int i = 0; i < steps.size(); ++i) f.steps.append({i + 1, steps[i]});
    w.updateField(f);
    return id;
}

// a reference field to the target type, paired with `inverse` there (the
// same name on the same type makes it symmetric)
Id ref(World &w, Id type, const QString &name, Id target, bool multi, const QString &inverse, bool inverseMulti = true)
{
    const Id id = w.addField(type, name, Kind::Reference);
    FieldDef f = *w.field(id);
    f.targets = {target};
    f.multi = multi;
    w.updateField(f);
    if (!inverse.isEmpty()) {
        const Id other = w.setInverse(id, inverse);
        if (other && other != id) {
            FieldDef o = *w.field(other);
            o.multi = inverseMulti;
            w.updateField(o);
        }
    }
    return id;
}

} // namespace

void addTypes(World &w)
{
    const Id character = w.addType("Character", "#c9a86a");
    const Id org = w.addType("Organization", "#6a9fc9");
    const Id location = w.addType("Location", "#7cb870");
    const Id event = w.addType("Event", "#c97a6a");
    const Id item = w.addType("Item", "#a98ac9");
    const Id thread = w.addType("Plot thread", "#c9c26a");
    const Id secret = w.addType("Secret", "#8a8a8a");

    plain(w, character, "Status", Kind::Text);
    plain(w, character, "Age", Kind::Number);
    ladder(w, character, "Cultivation",
           {"Mortal", "Qi Condensation", "Foundation Establishment", "Core Formation", "Nascent Soul"});
    plain(w, character, "Talent", Kind::Text);
    ref(w, character, "Master", character, false, "Disciples");
    ref(w, character, "Family", character, true, "Family");
    ref(w, character, "Allies", character, true, "Allies");
    ref(w, character, "Enemies", character, true, "Enemies");
    ref(w, character, "Affiliation", org, true, "Members");
    ref(w, character, "Lives in", location, false, "Residents");
    plain(w, character, "Traits", Kind::Tags);
    plain(w, character, "Details", Kind::LongText);
    plain(w, character, "Portrait", Kind::Image);

    ref(w, org, "Leader", character, false, "Leads");
    ref(w, org, "Headquarters", location, false, "Organizations");
    plain(w, org, "Details", Kind::LongText);

    ref(w, location, "Part of", location, false, "Places within");
    plain(w, location, "Details", Kind::LongText);

    ref(w, event, "Participants", character, true, "Events");
    ref(w, event, "Where", location, false, "Events here");
    plain(w, event, "Details", Kind::LongText);

    ref(w, item, "Owner", character, false, "Possessions");
    plain(w, item, "Grade", Kind::Text);
    plain(w, item, "Details", Kind::LongText);

    plain(w, thread, "Set up in", Kind::Chapter);
    plain(w, thread, "Paid off in", Kind::Chapter);
    ref(w, thread, "Involves", character, true, "Threads");
    plain(w, thread, "Details", Kind::LongText);

    // a link to a character made in a chapter is when they learn it
    plain(w, secret, "Truth", Kind::LongText);
    ref(w, secret, "Known by", character, true, "Knows");
    plain(w, secret, "Revealed to reader in", Kind::Chapter);
}

} // namespace worldsea::starter
