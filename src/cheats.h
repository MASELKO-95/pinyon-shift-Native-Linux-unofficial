#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

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

// The save editor (NP-8.3): when the title has just decrypted a profile,
// sets its credits to cheat_set_credits (once; -1 leaves them). The title
// then encodes the value itself, and its own saves keep it.
void EditLoadedProfile(uint8_t* body, size_t size);
// Called (on a guest thread) after a one-shot edit was applied, with the
// setting to clear so it does not apply again at the next start.
void SetAppliedCallback(std::function<void(std::string_view setting)> callback);

// Logs cheat.changed events for the settings the trainer changes; call once
// at startup.
void InstallChangeLog();

}  // namespace pinyon_shift::cheats
