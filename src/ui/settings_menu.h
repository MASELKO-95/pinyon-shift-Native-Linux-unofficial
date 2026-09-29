#pragma once

#include <filesystem>
#include <functional>
#include <memory>

#include "config/host_config.h"
#include "save_backups.h"
#include "ui/hostui/host_ui.h"
#include "ui/hostui/menu.h"

namespace pinyon_shift::ui {

// The in-game settings screen (NP-1.4): Display, Graphics, Audio, Controls
// and Profile pages over the host UI. Settings that apply live change their
// setting at once; the rest take effect at the next start and carry a
// RESTART badge. Every change is saved to `config` at once, with a backup
// before the first save of the session.
// What the settings screen can reach besides the settings file.
struct SettingsServices {
  // Builds the ACHIEVEMENTS page; no page without it.
  std::function<std::unique_ptr<hostui::MenuScreen>()> achievements;
  // Lists and restores save backups under PROFILE; no rows without it.
  SaveBackups* save_backups = nullptr;
  // <state>/mods: the MODS page lists its folders; no page when empty.
  std::filesystem::path mods_root;
  // The draw resolution scale the renderer uses (0 when unknown). With it,
  // RESOLUTION SCALE applies at once and asks for a restart only when the
  // renderer could not switch (Vulkan, or no shader pack for that scale).
  std::function<uint32_t()> draw_resolution_scale;
};

std::unique_ptr<hostui::MenuScreen> CreateSettingsMenu(hostui::HostUi& host_ui,
                                                       config::HostConfig& config,
                                                       SettingsServices services = {});

// The trainer (NP-8.2): World, Vehicle, Graphics and Debug pages, opened
// with F10 while cheats are on. Changes apply at once and are saved; each is
// logged as cheat.changed.
std::unique_ptr<hostui::MenuScreen> CreateTrainerMenu(hostui::HostUi& host_ui,
                                                      config::HostConfig& config);

// Master volume, 0 to 100, applied to the output mix.
void ApplyMasterVolume();

}  // namespace pinyon_shift::ui
