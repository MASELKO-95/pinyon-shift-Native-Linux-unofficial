#pragma once

#include <memory>

#include "ui/hostui/host_ui.h"
#include "ui/hostui/menu.h"

namespace pinyon_shift::ui {

// The in-game settings menu drawn by the host UI. NP-1.3 carries the settings
// that already apply live; NP-1.4 adds the full screen and saving.
std::unique_ptr<hostui::MenuScreen> CreateGameMenu(hostui::HostUi& host_ui);

}  // namespace pinyon_shift::ui
