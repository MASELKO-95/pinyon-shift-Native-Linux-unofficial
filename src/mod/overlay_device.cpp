#include "mod/overlay_device.h"

#include <rex/filesystem/entry.h>
#include <rex/logging.h>

#include "pinyon_shift_diagnostics.h"

namespace pinyon_shift::mod {

OverlayDevice::OverlayDevice(std::string_view mount_path, const std::filesystem::path& base_root,
                             std::vector<std::filesystem::path> overlay_roots)
    : Device(mount_path) {
  // Game files are read-only, the base and every overlay alike.
  base_ = std::make_unique<rex::filesystem::HostPathDevice>(mount_path, base_root, true);
  for (const auto& root : overlay_roots) {
    overlays_.push_back(
        std::make_unique<rex::filesystem::HostPathDevice>(mount_path, root, true));
  }
}

bool OverlayDevice::Initialize() {
  if (!base_->Initialize()) {
    return false;
  }
  for (auto it = overlays_.begin(); it != overlays_.end();) {
    if ((*it)->Initialize()) {
      ++it;
    } else {
      REXLOG_WARN("Mods: cannot read {}; its files are not used", (*it)->host_path().string());
      it = overlays_.erase(it);
    }
  }
  return true;
}

void OverlayDevice::Dump(rex::string::StringBuffer* string_buffer) {
  base_->Dump(string_buffer);
}

rex::filesystem::Entry* OverlayDevice::ResolvePath(std::string_view path) {
  if (!path.empty()) {
    for (const auto& overlay : overlays_) {
      rex::filesystem::Entry* entry = overlay->ResolvePath(path);
      // Only files replace; a mod's directories must not hide the game's.
      if (entry && !(entry->attributes() & rex::filesystem::kFileAttributeDirectory)) {
        diagnostics::RecordEvent("mod.file.override",
                                 {{"path", path}, {"from", overlay->host_path().string()}});
        return entry;
      }
    }
  }
  return base_->ResolvePath(path);
}

}  // namespace pinyon_shift::mod
