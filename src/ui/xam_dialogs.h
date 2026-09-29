#pragma once

#include <functional>
#include <memory>

#include <rex/kernel/xam/ui_provider.h>

namespace pinyon_shift::hostui {
class HostUi;
class MenuScreen;
}

namespace pinyon_shift::ui {

// The XAM message box and keyboard as host UI screens (NP-5.2): the title's
// fonts and layout, pad, keyboard and mouse input, B or Escape to cancel.
// `achievements` builds the list XamShowAchievementsUI opens.
std::unique_ptr<rex::kernel::xam::XamUiProvider> CreateXamDialogs(
    hostui::HostUi& host_ui, std::function<std::unique_ptr<hostui::MenuScreen>()> achievements);

}  // namespace pinyon_shift::ui
