#pragma once

#include <memory>

namespace rex::system {
class AchievementManager;
}

namespace pinyon_shift::hostui {
class MenuScreen;
}

namespace pinyon_shift::ui {

// The title's achievements (NP-5.3): one row per achievement with its
// gamerscore or LOCKED, the focused one's description under the list, and
// the unlocked count and gamerscore as the screen's text.
std::unique_ptr<hostui::MenuScreen> CreateAchievementsScreen(
    const rex::system::AchievementManager& achievements);

}  // namespace pinyon_shift::ui
