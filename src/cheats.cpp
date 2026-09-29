#include "cheats.h"

#include <algorithm>
#include <atomic>
#include <cmath>

#include <fmt/format.h>

#include <rex/cvar.h>

#include "pinyon_shift_diagnostics.h"

REXCVAR_DEFINE_BOOL(pinyon_shift_cheats, false, "Cheats",
                    "Enable the trainer (F10). The title then plays the separate modded profile "
                    "(<state>/user-modded), so cheated progress never reaches the player's own save")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_DOUBLE(cheat_time_scale, 1.0, "Cheats",
                      "Game speed: the title's gameplay delta is multiplied by this (0.25 to 2)")
    .range(0.25, 2.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pinyon_shift::cheats {

namespace {
std::atomic<bool> g_profile_isolated{false};
}  // namespace

bool Requested() { return REXCVAR_GET(pinyon_shift_cheats); }

void SetProfileIsolated() { g_profile_isolated.store(true, std::memory_order_release); }

bool Enabled() { return Requested() && g_profile_isolated.load(std::memory_order_acquire); }

double TimeScale() {
  if (!Enabled()) return 1.0;
  const double scale = REXCVAR_GET(cheat_time_scale);
  return std::isfinite(scale) ? std::clamp(scale, 0.25, 2.0) : 1.0;
}

std::string Active() {
  std::string active;
  if (TimeScale() != 1.0) {
    active += fmt::format("time_scale={:.2f}", TimeScale());
  }
  return active;
}

void InstallChangeLog() {
  // Every setting the trainer changes, including the graphics and debug ones
  // it shares with SETTINGS.
  for (const char* name : {"cheat_time_scale", "disable_motion_blur", "disable_depth_of_field",
                           "force_trilinear_filtering", "fh1_render_test_log_file_opens"}) {
    rex::cvar::RegisterChangeCallback(name, [](std::string_view name, std::string_view value) {
      diagnostics::RecordEvent("cheat.changed", {{"name", name}, {"value", value}});
    });
  }
}

}  // namespace pinyon_shift::cheats
