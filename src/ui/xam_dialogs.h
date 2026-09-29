#pragma once

#include <memory>

#include <rex/kernel/xam/ui_provider.h>

namespace pinyon_shift::hostui {
class HostUi;
}

namespace pinyon_shift::ui {

// The XAM message box and keyboard as host UI screens (NP-5.2): the title's
// fonts and layout, pad, keyboard and mouse input, B or Escape to cancel.
std::unique_ptr<rex::kernel::xam::XamUiProvider> CreateXamDialogs(hostui::HostUi& host_ui);

}  // namespace pinyon_shift::ui
