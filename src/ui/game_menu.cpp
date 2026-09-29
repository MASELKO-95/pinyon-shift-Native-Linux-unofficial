#include "ui/game_menu.h"

#include <string>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/string.h>

namespace pinyon_shift::ui {
namespace {

bool GetBool(const char* name) {
  return rex::string::from_string<bool>(rex::cvar::GetFlagByName(name), false);
}

hostui::MenuRow ToggleRow(std::string label, const char* cvar) {
  hostui::MenuRow row;
  row.label = std::move(label);
  row.value = [cvar] { return std::string(GetBool(cvar) ? "ON" : "OFF"); };
  row.adjust = [cvar](int) { rex::cvar::SetFlagByName(cvar, GetBool(cvar) ? "false" : "true"); };
  return row;
}

}  // namespace

std::unique_ptr<hostui::MenuScreen> CreateGameMenu(hostui::HostUi& host_ui) {
  std::vector<hostui::MenuRow> rows;
  hostui::MenuRow resume;
  resume.label = "RESUME";
  resume.activate = [&host_ui] { host_ui.Close(); };
  rows.push_back(std::move(resume));
  rows.push_back(ToggleRow("FULLSCREEN", "fullscreen"));
  rows.push_back(ToggleRow("MOUSE AND KEYBOARD", "mnk_mode"));
  return std::make_unique<hostui::MenuScreen>("SETTINGS", std::move(rows));
}

}  // namespace pinyon_shift::ui
