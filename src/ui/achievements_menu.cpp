#include "ui/achievements_menu.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include <rex/system/achievement_manager.h>

#include "ui/hostui/menu.h"

namespace pinyon_shift::ui {
namespace {

std::string Upper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return c < 0x80 ? char(std::toupper(c)) : char(c);
  });
  return text;
}

}  // namespace

std::unique_ptr<hostui::MenuScreen> CreateAchievementsScreen(
    const rex::system::AchievementManager& achievements) {
  const auto list = achievements.ListAchievements();
  struct Entry {
    std::string description;
  };
  auto entries = std::make_shared<std::vector<Entry>>();
  std::vector<hostui::MenuRow> rows;
  uint32_t unlocked = 0, earned = 0, total = 0;
  for (const auto& achievement : list) {
    const bool is_unlocked = achievements.IsUnlocked(achievement.id);
    total += achievement.gamerscore;
    if (is_unlocked) {
      ++unlocked;
      earned += achievement.gamerscore;
    }
    hostui::MenuRow row;
    row.label = Upper(achievement.label);
    row.value = [is_unlocked, score = achievement.gamerscore] {
      return is_unlocked ? std::to_string(score) + "G" : std::string("LOCKED");
    };
    rows.push_back(std::move(row));
    entries->push_back({!is_unlocked && !achievement.unachieved_description.empty()
                            ? achievement.unachieved_description
                            : achievement.description});
  }
  auto self = std::make_shared<const hostui::MenuScreen*>(nullptr);
  auto screen = std::make_unique<hostui::MenuScreen>(
      "ACHIEVEMENTS", std::move(rows), [self, entries]() -> std::string {
        if (!*self || entries->empty()) {
          return entries->empty() ? std::string("THE TITLE LISTS NO ACHIEVEMENTS") : std::string();
        }
        return (*entries)[(*self)->focus()].description;
      });
  screen->set_body(std::to_string(unlocked) + " of " + std::to_string(list.size()) +
                   " unlocked, " + std::to_string(earned) + " of " + std::to_string(total) +
                   " gamerscore");
  *self = screen.get();
  return screen;
}

}  // namespace pinyon_shift::ui
