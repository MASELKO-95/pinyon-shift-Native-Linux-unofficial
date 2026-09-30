#include "cheats.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

#include <fmt/format.h>

#include <rex/cvar.h>

#include "pinyon_shift_diagnostics.h"
#include "save/profile_body.h"

REXCVAR_DEFINE_BOOL(pinyon_shift_cheats, false, "Cheats",
                    "Enable the trainer (F10). The title then plays the separate modded profile "
                    "(<state>/user-modded), so cheated progress never reaches the player's own save")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_DOUBLE(cheat_time_scale, 1.0, "Cheats",
                      "Game speed: the title's gameplay delta is multiplied by this (0.25 to 2)")
    .range(0.25, 2.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(cheat_time_of_day, -1.0, "Cheats",
                      "Hold the time of day at this hour (0 to 24, fractions allowed); -1 lets "
                      "the title's clock run")
    .range(-1.0, 24.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(cheat_free_camera, false, "Cheats",
                    "Switch the cameras to the title's free camera (moved with the pad); off "
                    "returns them to the player")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(cheat_set_credits, -1, "Cheats",
                     "Set the profile's credits to this when it next loads, once; -1 leaves them")
    .range(-1, 999999999)
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace pinyon_shift::cheats {

namespace {
std::atomic<bool> g_profile_isolated{false};
std::atomic<bool> g_credits_applied{false};
std::atomic<int32_t> g_set_credits_value{-1};
std::mutex g_applied_mutex;
std::function<void(std::string_view)> g_applied;

void StoreBe32(uint8_t* bytes, uint32_t value) {
  bytes[0] = uint8_t(value >> 24);
  bytes[1] = uint8_t(value >> 16);
  bytes[2] = uint8_t(value >> 8);
  bytes[3] = uint8_t(value);
}
}  // namespace

bool Requested() { return REXCVAR_GET(pinyon_shift_cheats); }

void SetProfileIsolated() { g_profile_isolated.store(true, std::memory_order_release); }

bool Enabled() { return Requested() && g_profile_isolated.load(std::memory_order_acquire); }

double TimeScale() {
  if (!Enabled()) return 1.0;
  const double scale = REXCVAR_GET(cheat_time_scale);
  return std::isfinite(scale) ? std::clamp(scale, 0.25, 2.0) : 1.0;
}

double TimeOfDaySeconds() {
  if (!Enabled()) return -1.0;
  const double hours = REXCVAR_GET(cheat_time_of_day);
  return std::isfinite(hours) && hours >= 0.0 ? std::min(hours, 24.0) * 3600.0 : -1.0;
}

bool FreeCamera() { return Enabled() && REXCVAR_GET(cheat_free_camera); }

std::string Active() {
  std::string active;
  if (TimeScale() != 1.0) {
    active += fmt::format("time_scale={:.2f}", TimeScale());
  }
  if (FreeCamera()) {
    active += fmt::format("{}free_camera", active.empty() ? "" : ",");
  }
  if (const double seconds = TimeOfDaySeconds(); seconds >= 0.0) {
    active += fmt::format("{}time_of_day={:.2f}", active.empty() ? "" : ",", seconds / 3600.0);
  }
  if (g_credits_applied.load(std::memory_order_acquire)) {
    active += fmt::format("{}set_credits={}", active.empty() ? "" : ",",
                          g_set_credits_value.load(std::memory_order_acquire));
  }
  return active;
}

void EditLoadedProfile(uint8_t* body, size_t size) {
  const int32_t credits = REXCVAR_GET(cheat_set_credits);
  if (!Enabled() || credits < 0 || g_credits_applied.load(std::memory_order_acquire)) {
    return;
  }
  // Other secure files pass through here too; only the profile has the field.
  const auto field = save::FindProfileField(body, size, "Main/Credits");
  if (!field || field->type != save::FieldType::kUInt32) {
    return;
  }
  if (g_credits_applied.exchange(true, std::memory_order_acq_rel)) {
    return;
  }
  uint8_t* value = body + field->offset;
  const uint32_t previous = (uint32_t(value[0]) << 24) | (uint32_t(value[1]) << 16) |
                            (uint32_t(value[2]) << 8) | uint32_t(value[3]);
  StoreBe32(value, static_cast<uint32_t>(credits));
  g_set_credits_value.store(credits, std::memory_order_release);
  diagnostics::RecordEvent("cheat.applied", {{"name", "cheat_set_credits"},
                                             {"previous", fmt::format("{}", previous)},
                                             {"value", fmt::format("{}", credits)}});
  std::function<void(std::string_view)> applied;
  {
    std::lock_guard lock(g_applied_mutex);
    applied = g_applied;
  }
  if (applied) applied("cheat_set_credits");
}

void SetAppliedCallback(std::function<void(std::string_view setting)> callback) {
  std::lock_guard lock(g_applied_mutex);
  g_applied = std::move(callback);
}

void InstallChangeLog() {
  // Every setting the trainer changes, including the graphics and debug ones
  // it shares with SETTINGS.
  for (const char* name : {"cheat_time_scale", "cheat_time_of_day", "cheat_free_camera",
                           "disable_motion_blur",
                           "disable_depth_of_field",
                           "force_trilinear_filtering", "fh1_render_test_log_file_opens"}) {
    rex::cvar::RegisterChangeCallback(name, [](std::string_view name, std::string_view value) {
      diagnostics::RecordEvent("cheat.changed", {{"name", name}, {"value", value}});
    });
  }
}

}  // namespace pinyon_shift::cheats
