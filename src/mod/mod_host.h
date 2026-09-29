#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "pinyon_mod.h"

namespace pinyon_shift::config {
class HostConfig;
}

namespace pinyon_shift::mod {

// Hook dispatch (NP-7.1), called from the title's hook sites on guest
// threads. Cheap when no mod subscribed.
bool HasSubscribers(PinyonHook hook);
void Dispatch(const PinyonHookEvent& event);
// Runs the guest tasks mods queued; at frame.tick, on the title's main thread.
void RunGuestTasks();

// What the host gives the mod host besides the files.
struct HostServices {
  config::HostConfig* config = nullptr;  // saved values of mods' own settings
  // Runs `task` on the UI thread (for dialogs).
  std::function<void(std::function<void()> task)> post_to_ui;
};

// Discovery, validation, loading and lifecycle of mods (NP-7.3).
struct ModInfo {
  std::string name;
  std::string version;
  std::filesystem::path directory;
  bool loaded = false;
  std::string problem;  // why it was not loaded
};

// Parses `enabled_mods`, validates each mods/<name>/mod.toml (ABI, game
// version, requires, conflicts), orders them (requires and load_after, else
// the list order), and loads their libraries. Mods are never unloaded.
void LoadMods(const std::filesystem::path& state_root, const std::string& enabled_mods,
              HostServices services);
const std::vector<ModInfo>& Mods();
bool AnyModLoaded();
// Directories of loaded mods' game/ overrides, in priority order.
std::vector<std::filesystem::path> OverlayRoots();

// Profile isolation (NP-7.5). With mods enabled the title plays the separate
// `user_root` profile (<state>/user-modded); each save then writes
// pinyon_shift_mods.json there with the enabled mods, the mod-set hash and
// the plaintext save body hash.
void SetModdedProfile(std::filesystem::path user_root);
bool ModdedProfile();
// At save.before_encrypt: tags the modded profile's save.
void RecordSave(uint32_t body_address, uint32_t body_size);
// A stable hash of the loaded mods' names and versions.
std::string ModSetHash();

void NotifyCreateDialogs();
void NotifyModuleLaunched();
void NotifyShutdown();

}  // namespace pinyon_shift::mod
