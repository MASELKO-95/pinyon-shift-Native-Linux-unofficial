#pragma once

#include <memory>

#include "config/host_config.h"
#include "ui/hostui/host_ui.h"
#include "ui/hostui/menu.h"

namespace pinyon_shift::ui {

// The in-game settings screen (NP-1.4): Display, Graphics, Audio, Controls
// and Profile pages over the host UI. Settings that apply live change their
// setting at once; the rest take effect at the next start and carry a
// RESTART badge. Every change is saved to `config` at once, with a backup
// before the first save of the session.
std::unique_ptr<hostui::MenuScreen> CreateSettingsMenu(hostui::HostUi& host_ui,
                                                       config::HostConfig& config);

// Master volume, 0 to 100, applied to the output mix.
void ApplyMasterVolume();

}  // namespace pinyon_shift::ui
