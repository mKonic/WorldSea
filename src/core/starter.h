#pragma once
// The types a new world starts with: Character, Organization, Location,
// Event, Item, Plot thread and Secret, with sheets and paired reference
// fields. All of it is ordinary data the writer can rename, extend or delete.

namespace worldsea {

class World;

namespace starter {
void addTypes(World &w);
}

} // namespace worldsea
