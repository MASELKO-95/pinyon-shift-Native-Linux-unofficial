#pragma once

namespace pinyon_shift::cheats {

// Map markers for the hidden collectibles (NP-8.6): whether the map should
// show every discount sign and barn find (cheat_show_collectibles, with
// cheats on).
bool ShowCollectibles();

// Called once per title frame (frame.tick, before the guest tasks run): while
// the cheat is on, queues a pass that puts the not-yet-found collectibles'
// markers on the title's map and minimap, and one that takes them off again
// when it is turned off.
void UpdateCollectibleMarkers();

}  // namespace pinyon_shift::cheats
