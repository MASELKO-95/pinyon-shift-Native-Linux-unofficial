#pragma once

namespace pinyon_shift::dlc {

// The Treasure Map add-on (pinyon_shift_dlc_treasure_map). Called once per
// title frame (frame.tick, before the guest tasks run): while the setting is
// on, queues a check twice a second that runs the title's own Treasure Map
// reveal once free roam's collectibles are live and not all revealed yet.
void UpdateTreasureMap();

// Whether the world's game mode object exists: sub_828BC9D8 (collectibles
// live) reads it through sub_824878D0 without a check, and while the title
// loads or leaves free roam it is null, so a host call then reads guest 0x38
// and stops the game. True when a call is safe.
bool GameModeReady();

}  // namespace pinyon_shift::dlc
