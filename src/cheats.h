#pragma once

#include <string>

namespace pinyon_shift::cheats {

// The trainer (NP-8). Cheats only apply with pinyon_shift_cheats on, which
// also switches the title to the separate modded profile, so a cheated save
// never replaces the player's own: they stay off until that profile is in use.
bool Enabled();

// Whether the player turned cheats on (config file or command line).
bool Requested();
// Called once the title's profile is the separate modded one.
void SetProfileIsolated();

// The factor applied to the title's gameplay delta, 1 when cheats are off.
double TimeScale();

// The cheats currently changing the game, as "name=value" pairs separated by
// commas ("" when none), for the modded profile's save tag.
std::string Active();

// Logs cheat.changed events for the settings the trainer changes; call once
// at startup.
void InstallChangeLog();

}  // namespace pinyon_shift::cheats
