#pragma once

#include <imgui.h>

#include <rex/ui/style.h>

namespace pinyon_shift::ui {

// Interim player-facing look for the SDK's ImGui surfaces (XAM message box,
// virtual keyboard, achievement toast, achievement list) until the host UI
// layer draws them with the game's own assets (NP-1.1, NP-5).

// Adds a readable system font rasterized for `dpi_scale` and makes it the
// default. Keeps the SDK's debug font when no system font is found.
void ConfigureHostFonts(ImFontAtlas* atlas, float dpi_scale);

// Applies the palette and spacing on top of the SDK defaults.
void ConfigureHostStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style);

}  // namespace pinyon_shift::ui
