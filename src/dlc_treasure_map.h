#pragma once

namespace pinyon_shift::dlc {

// The Treasure Map add-on (pinyon_shift_dlc_treasure_map). Called once per
// title frame (frame.tick, before the guest tasks run): while the setting is
// on, queues a check twice a second that runs the title's own Treasure Map
// reveal once free roam's collectibles are live and not all revealed yet.
void UpdateTreasureMap();

}  // namespace pinyon_shift::dlc
