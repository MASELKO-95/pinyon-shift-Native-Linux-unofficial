#include "ui/settings_menu.h"

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <memory>
#include <cctype>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <rex/audio/downmix.h>
#include <rex/input/pad_remap.h>

#include "mod/mod_host.h"
#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(pinyon_shift_master_volume, 100, "Pinyon Shift",
                     "Master volume, 0 to 100, applied to the output mix");

namespace pinyon_shift::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;

// One choice of a setting: the label shown and the TOML literal each
// setting takes (several for choices such as the resolution scale, which
// sets both axes).
struct Choice {
  std::string label;
  std::vector<std::pair<std::string, std::string>> values;
};

std::string Unquote(std::string_view literal) {
  if (literal.size() >= 2 && literal.front() == '"' && literal.back() == '"') {
    literal = literal.substr(1, literal.size() - 2);
  }
  return std::string(literal);
}

// Whole-string number, so "-0.5" and "-0.500000" compare equal.
std::optional<double> Number(std::string_view text) {
  const std::string copy(text);
  char* end = nullptr;
  const double value = std::strtod(copy.c_str(), &end);
  if (copy.empty() || end != copy.c_str() + copy.size()) {
    return std::nullopt;
  }
  return value;
}

bool SameValue(std::string_view a, std::string_view b) {
  const auto number_a = Number(a);
  const auto number_b = Number(b);
  if (number_a && number_b) {
    return *number_a == *number_b;
  }
  return a.size() == b.size() &&
         std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) ==
                  std::tolower(static_cast<unsigned char>(y));
         });
}

std::string Upper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return char(std::toupper(c)); });
  return text;
}

// Screens of one opening of the menu. The root screen's rows own the pages
// (every other screen sits above the root on the host UI's stack), so each
// function here may capture `this`.
class SettingsPages : public std::enable_shared_from_this<SettingsPages> {
 public:
  SettingsPages(hostui::HostUi& host_ui, config::HostConfig& config, SettingsServices services)
      : host_ui_(host_ui), config_(config), services_(std::move(services)) {}

  std::unique_ptr<MenuScreen> Root();

 private:
  // The value the game started with (what is live for restart settings).
  static std::string Live(const std::string& name) { return rex::cvar::GetFlagByName(name); }
  // The saved value, or the live one when the file does not set it.
  std::string Saved(const std::string& name) const {
    return config_.Get(name).value_or(Live(name));
  }

  static int Find(const std::vector<Choice>& choices, auto&& lookup) {
    for (size_t i = 0; i < choices.size(); ++i) {
      bool all = true;
      for (const auto& [name, literal] : choices[i].values) {
        all = all && SameValue(lookup(name), Unquote(literal));
      }
      if (all) {
        return int(i);
      }
    }
    return -1;
  }

  MenuRow Setting(std::string label, std::vector<Choice> choices, bool restart);
  MenuRow Toggle(std::string label, std::string name, bool restart = false, bool inverted = false);
  MenuRow Page(std::string label, std::unique_ptr<MenuScreen> (SettingsPages::*page)());
  std::function<std::string()> RestartNote(std::vector<MenuRow>& rows);
  void Save();

  std::unique_ptr<MenuScreen> Display();
  std::unique_ptr<MenuScreen> Graphics();
  std::unique_ptr<MenuScreen> Audio();
  std::unique_ptr<MenuScreen> Controls();
  std::unique_ptr<MenuScreen> Profile();
  std::unique_ptr<MenuScreen> Gamertag();
  std::unique_ptr<MenuScreen> Backups();
  std::unique_ptr<MenuScreen> ControllerButtons();
  std::unique_ptr<MenuScreen> Mods();
  std::unique_ptr<MenuScreen> ConfirmRestore(std::string slot);
  static constexpr size_t kMaxGamertag = 15;

  hostui::HostUi& host_ui_;
  config::HostConfig& config_;
  SettingsServices services_;
};

MenuRow SettingsPages::Setting(std::string label, std::vector<Choice> choices, bool restart) {
  MenuRow row;
  row.label = std::move(label);
  row.restart_required = restart;
  auto shared = std::make_shared<std::vector<Choice>>(std::move(choices));
  row.value = [this, shared] {
    const int index = Find(*shared, [this](const std::string& name) { return Saved(name); });
    return index >= 0 ? (*shared)[size_t(index)].label : std::string("CUSTOM");
  };
  row.adjust = [this, shared, restart](int direction) {
    const auto& choices = *shared;
    const int count = int(choices.size());
    const int current = Find(choices, [this](const std::string& name) { return Saved(name); });
    const int next = current < 0 ? 0 : (current + direction + count) % count;
    for (const auto& [name, literal] : choices[size_t(next)].values) {
      config_.Set(name, literal);
      if (!restart) {
        rex::cvar::SetFlagByName(name, Unquote(literal));
      }
    }
    Save();
  };
  if (restart) {
    row.restart_pending = [this, shared] {
      const auto saved = Find(*shared, [this](const std::string& name) { return Saved(name); });
      const auto live = Find(*shared, [](const std::string& name) { return Live(name); });
      return saved != live;
    };
  }
  return row;
}

MenuRow SettingsPages::Toggle(std::string label, std::string name, bool restart, bool inverted) {
  const char* on = inverted ? "false" : "true";
  const char* off = inverted ? "true" : "false";
  return Setting(std::move(label), {{"OFF", {{name, off}}}, {"ON", {{name, on}}}}, restart);
}

MenuRow SettingsPages::Page(std::string label,
                            std::unique_ptr<MenuScreen> (SettingsPages::*page)()) {
  MenuRow row;
  row.label = std::move(label);
  row.activate = [self = shared_from_this(), page] { self->host_ui_.Push(((*self).*page)()); };
  return row;
}

std::function<std::string()> SettingsPages::RestartNote(std::vector<MenuRow>& rows) {
  std::vector<std::function<bool()>> pending;
  for (const MenuRow& row : rows) {
    if (row.restart_pending) {
      pending.push_back(row.restart_pending);
    }
  }
  if (pending.empty()) {
    return nullptr;
  }
  return [pending] {
    for (const auto& check : pending) {
      if (check()) {
        return std::string("RESTART THE GAME TO APPLY THE MARKED CHANGES");
      }
    }
    return std::string();
  };
}

void SettingsPages::Save() {
  if (!config_.Save()) {
    REXLOG_ERROR("Settings: could not write {}", config_.path().string());
  }
}

std::unique_ptr<MenuScreen> SettingsPages::Display() {
  std::vector<MenuRow> rows;
  rows.push_back(Toggle("FULLSCREEN", "fullscreen"));
  rows.push_back(Setting("MONITOR",
                         {{"DEFAULT", {{"monitor", "0"}}},
                          {"1", {{"monitor", "1"}}},
                          {"2", {{"monitor", "2"}}},
                          {"3", {{"monitor", "3"}}}},
                         true));
  std::vector<Choice> sizes = {{"DEFAULT", {{"window_width", "0"}, {"window_height", "0"}}}};
  for (const auto& [width, height] : {std::pair{1280, 720}, std::pair{1600, 900},
                                      std::pair{1920, 1080}, std::pair{2560, 1440},
                                      std::pair{3840, 2160}}) {
    sizes.push_back({std::to_string(width) + "X" + std::to_string(height),
                     {{"window_width", std::to_string(width)},
                      {"window_height", std::to_string(height)}}});
  }
  rows.push_back(Setting("WINDOW SIZE", std::move(sizes), true));
  // Letterbox keeps the guest's aspect with bars, crop fills the window by
  // cutting into the title's overscan margin, stretch fills it by scaling.
  rows.push_back(Setting("ASPECT RATIO",
                         {{"LETTERBOX", {{"present_letterbox", "true"},
                                         {"present_allow_overscan_cutoff", "false"}}},
                          {"CROP", {{"present_letterbox", "true"},
                                    {"present_allow_overscan_cutoff", "true"}}},
                          {"STRETCH", {{"present_letterbox", "false"},
                                       {"present_allow_overscan_cutoff", "false"}}}},
                         true));
  rows.push_back(Toggle("VSYNC", "vsync"));
  rows.push_back(Setting("FRAME RATE LIMIT",
                         {{"OFF", {{"host_present_fps_limit", "0"}}},
                          {"30", {{"host_present_fps_limit", "30"}}},
                          {"60", {{"host_present_fps_limit", "60"}}},
                          {"120", {{"host_present_fps_limit", "120"}}},
                          {"240", {{"host_present_fps_limit", "240"}}}},
                         false));
  rows.push_back(Toggle("VARIABLE REFRESH RATE", "d3d12_allow_variable_refresh_rate_and_tearing"));
  // How the rendered image is scaled to the window: FSR 1 and CAS keep 2x
  // on a 4K display and 3x on 1440p sharp where bilinear blurs.
  rows.push_back(Setting("OUTPUT SCALING",
                         {{"BILINEAR", {{"present_effect", "\"bilinear\""}}},
                          {"CAS", {{"present_effect", "\"cas\""}}},
                          {"FSR 1", {{"present_effect", "\"fsr\""}}}},
                         true));
  rows.push_back(Setting("GAME FRAME RATE LIMIT",
                         {{"OFF", {{"pinyon_shift_fh1_render_fps_limit", "0"}}},
                          {"30", {{"pinyon_shift_fh1_render_fps_limit", "30"}}},
                          {"60", {{"pinyon_shift_fh1_render_fps_limit", "60"}}},
                          {"120", {{"pinyon_shift_fh1_render_fps_limit", "120"}}}},
                         false));
  auto note = RestartNote(rows);
  return std::make_unique<MenuScreen>("DISPLAY", std::move(rows), std::move(note));
}

std::unique_ptr<MenuScreen> SettingsPages::Graphics() {
  std::vector<MenuRow> rows;
  std::vector<Choice> scales;
  for (int scale = 1; scale <= 4; ++scale) {
    const std::string value = std::to_string(scale);
    scales.push_back({value + "X",
                      {{"draw_resolution_scale_x", value}, {"draw_resolution_scale_y", value}}});
  }
  rows.push_back(Setting("RESOLUTION SCALE", std::move(scales), true));
  // anisotropic_override holds the Xenos filter: 3, 4 and 5 are 4x, 8x, 16x.
  rows.push_back(Setting("ANISOTROPIC FILTERING",
                         {{"4X", {{"anisotropic_override", "3"}}},
                          {"8X", {{"anisotropic_override", "4"}}},
                          {"16X", {{"anisotropic_override", "5"}}}},
                         false));
  rows.push_back(Toggle("TRILINEAR FILTERING", "force_trilinear_filtering"));
  rows.push_back(Setting("TEXTURE DETAIL",
                         {{"SOFT", {{"texture_mip_lod_bias", "0.5"}}},
                          {"DEFAULT", {{"texture_mip_lod_bias", "0.0"}}},
                          {"SHARP", {{"texture_mip_lod_bias", "-0.5"}}},
                          {"SHARPEST", {{"texture_mip_lod_bias", "-1.0"}}}},
                         true));
  rows.push_back(Setting("ANTI-ALIASING",
                         {{"OFF", {{"swap_post_effect", "\"none\""}}},
                          {"FXAA", {{"swap_post_effect", "\"fxaa\""}}},
                          {"FXAA EXTREME", {{"swap_post_effect", "\"fxaa_extreme\""}}}},
                         false));
  rows.push_back(Toggle("MOTION BLUR", "disable_motion_blur", false, true));
  rows.push_back(Toggle("DEPTH OF FIELD", "disable_depth_of_field", false, true));
  auto note = RestartNote(rows);
  return std::make_unique<MenuScreen>("GRAPHICS", std::move(rows), std::move(note));
}

std::unique_ptr<MenuScreen> SettingsPages::Audio() {
  std::vector<MenuRow> rows;
  std::vector<Choice> volumes;
  for (int volume = 0; volume <= 100; volume += 10) {
    volumes.push_back({std::to_string(volume), {{"pinyon_shift_master_volume",
                                                 std::to_string(volume)}}});
  }
  rows.push_back(Setting("MASTER VOLUME", std::move(volumes), false));
  rows.push_back(Toggle("MUTE", "audio_mute"));
  return std::make_unique<MenuScreen>("AUDIO", std::move(rows));
}

std::unique_ptr<MenuScreen> SettingsPages::ControllerButtons() {
  // One row per physical control, showing what the title receives from it;
  // swapping A and B is A SENDS B and B SENDS A. Menus here keep the
  // physical layout whatever is set.
  using rex::input::PadControl;
  std::vector<MenuRow> rows;
  for (size_t i = 0; i < rex::input::kPadControlCount; ++i) {
    MenuRow row;
    row.label = std::string(rex::input::PadControlName(PadControl(i))) + " SENDS";
    row.value = [this, i] {
      const auto remap = rex::input::ParsePadRemap(Unquote(Saved("pad_remap")));
      return std::string(rex::input::PadControlName(remap[i]));
    };
    row.adjust = [this, i](int direction) {
      auto remap = rex::input::ParsePadRemap(Unquote(Saved("pad_remap")));
      const size_t count = rex::input::kPadControlCount;
      remap[i] = PadControl((size_t(remap[i]) + count + (direction > 0 ? 1 : count - 1)) % count);
      const std::string text = rex::input::FormatPadRemap(remap);
      config_.Set("pad_remap", config::Quote(text));
      rex::cvar::SetFlagByName("pad_remap", text);
      Save();
    };
    rows.push_back(std::move(row));
  }
  rows.push_back(Toggle("INVERT LOOK", "pad_invert_right_stick_y"));
  MenuRow reset;
  reset.label = "RESET TO DEFAULT";
  reset.activate = [this] {
    config_.Set("pad_remap", config::Quote(""));
    rex::cvar::SetFlagByName("pad_remap", "");
    Save();
  };
  rows.push_back(std::move(reset));
  return std::make_unique<MenuScreen>("CONTROLLER", std::move(rows), [this] {
    return rex::input::FormatPadRemap(rex::input::ParsePadRemap(Unquote(Saved("pad_remap"))))
                   .empty()
               ? std::string("EVERY BUTTON SENDS ITSELF")
               : std::string("CHANGES APPLY AT ONCE; MENUS HERE KEEP THE PHYSICAL LAYOUT");
  });
}

std::unique_ptr<MenuScreen> SettingsPages::Mods() {
  // One row per folder in <state>/mods, switched on and off in enabled_mods
  // (at the next start); the note under the list is the focused mod's state.
  std::vector<std::string> names;
  std::error_code error;
  for (auto it = std::filesystem::directory_iterator(services_.mods_root, error);
       !error && it != std::filesystem::directory_iterator(); it.increment(error)) {
    if (it->is_directory(error)) names.push_back(it->path().filename().string());
  }
  std::sort(names.begin(), names.end());
  const auto enabled_list = [this] {
    std::vector<std::string> list;
    std::string text = Unquote(Saved("enabled_mods"));
    size_t start = 0;
    while (start <= text.size()) {
      const size_t comma = text.find(',', start);
      std::string name = text.substr(start, comma == std::string::npos ? std::string::npos
                                                                       : comma - start);
      if (!name.empty()) list.push_back(name);
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
    return list;
  };
  std::vector<MenuRow> rows;
  for (const auto& name : names) {
    MenuRow row;
    row.label = Upper(name);
    row.restart_required = true;
    row.value = [enabled_list, name] {
      const auto list = enabled_list();
      return std::string(std::find(list.begin(), list.end(), name) != list.end() ? "ON" : "OFF");
    };
    row.adjust = [this, enabled_list, name](int) {
      auto list = enabled_list();
      const auto it = std::find(list.begin(), list.end(), name);
      if (it != list.end()) {
        list.erase(it);
      } else {
        list.push_back(name);
      }
      std::string text;
      for (const auto& entry : list) text += (text.empty() ? "" : ",") + entry;
      config_.Set("enabled_mods", config::Quote(text));
      Save();
    };
    rows.push_back(std::move(row));
  }
  auto self = std::make_shared<const MenuScreen*>(nullptr);
  auto screen = std::make_unique<MenuScreen>("MODS", std::move(rows), [self, names] {
    if (names.empty()) return std::string("PUT MODS IN THE MODS FOLDER OF THE GAME'S STATE");
    const std::string& name = names[(*self)->focus()];
    for (const auto& info : pinyon_shift::mod::Mods()) {
      if (info.name == name) {
        return info.loaded ? std::string("LOADED ") + info.version
                           : std::string("NOT LOADED: ") + Upper(info.problem);
      }
    }
    return std::string("MODS PLAY A SEPARATE PROFILE; CHANGES APPLY AT THE NEXT START");
  });
  *self = screen.get();
  return screen;
}

std::unique_ptr<MenuScreen> SettingsPages::Controls() {
  std::vector<MenuRow> rows;
  MenuRow controller;
  controller.label = "CONTROLLER BUTTONS";
  controller.activate = [this] { host_ui_.Push(ControllerButtons()); };
  rows.push_back(std::move(controller));
  rows.push_back(Setting("RUMBLE",
                         {{"OFF", {{"pad_rumble_strength", "0"}}},
                          {"25%", {{"pad_rumble_strength", "25"}}},
                          {"50%", {{"pad_rumble_strength", "50"}}},
                          {"75%", {{"pad_rumble_strength", "75"}}},
                          {"100%", {{"pad_rumble_strength", "100"}}}},
                         false));
  rows.push_back(Toggle("MOUSE AND KEYBOARD", "mnk_mode"));
  rows.push_back(Setting("MOUSE",
                         {{"OFF", {{"mnk_mouse", "false"}, {"mnk_mouse_steering", "false"}}},
                          {"CAMERA", {{"mnk_mouse", "true"}, {"mnk_mouse_steering", "false"}}},
                          {"STEERING", {{"mnk_mouse", "false"}, {"mnk_mouse_steering", "true"}}}},
                         false));
  std::vector<Choice> sensitivities;
  for (const char* value : {"0.25", "0.5", "0.75", "1.0", "1.5", "2.0", "3.0"}) {
    sensitivities.push_back({value, {{"mnk_sensitivity", value}}});
  }
  rows.push_back(Setting("MOUSE SENSITIVITY", std::move(sensitivities), false));
  // The keys each pad control maps to in mouse-and-keyboard mode. Editing
  // them is NP-6.1.
  const std::pair<const char*, const char*> binds[] = {
      {"A", "keybind_a"},
      {"B", "keybind_b"},
      {"X", "keybind_x"},
      {"Y", "keybind_y"},
      {"LEFT TRIGGER", "keybind_left_trigger"},
      {"RIGHT TRIGGER", "keybind_right_trigger"},
      {"LEFT BUMPER", "keybind_left_shoulder"},
      {"RIGHT BUMPER", "keybind_right_shoulder"},
      {"STEER LEFT", "keybind_lstick_left"},
      {"STEER RIGHT", "keybind_lstick_right"},
      {"BACK", "keybind_back"},
      {"START", "keybind_start"},
  };
  for (const auto& [label, name] : binds) {
    MenuRow row;
    row.label = label;
    row.value = [this, name = std::string(name)] {
      std::string keys = Upper(Saved(name));
      for (size_t at = keys.find(','); at != std::string::npos; at = keys.find(',', at + 3)) {
        keys.replace(at, 1, " / ");
      }
      return keys.empty() ? std::string("NONE") : keys;
    };
    rows.push_back(std::move(row));
  }
  return std::make_unique<MenuScreen>("CONTROLS", std::move(rows));
}

std::unique_ptr<MenuScreen> SettingsPages::Gamertag() {
  // Edited one character at a time so the pad works as well as the
  // keyboard: LETTER cycles the character at POSITION, SAVE writes it.
  struct Draft {
    std::string name;
    size_t cursor = 0;
  };
  auto draft = std::make_shared<Draft>();
  draft->name = Unquote(Saved("user_name"));
  draft->cursor = draft->name.size() < kMaxGamertag ? draft->name.size() : kMaxGamertag - 1;
  static constexpr std::string_view kCharacters =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ";
  std::vector<MenuRow> rows(5);
  rows[0].label = "NAME";
  rows[0].value = [draft] { return draft->name.empty() ? std::string("-") : draft->name; };
  rows[0].enabled = [] { return false; };
  rows[1].label = "LETTER";
  rows[1].value = [draft] {
    return draft->cursor < draft->name.size() ? std::string(1, draft->name[draft->cursor])
                                              : std::string("+");
  };
  rows[1].adjust = [draft](int direction) {
    const size_t count = kCharacters.size();
    if (draft->cursor >= draft->name.size()) {
      draft->name.push_back(direction > 0 ? kCharacters.front() : kCharacters.back());
      return;
    }
    const size_t index = kCharacters.find(draft->name[draft->cursor]);
    const size_t next = index == std::string_view::npos
                            ? 0
                            : (index + count + size_t(direction > 0 ? 1 : count - 1)) % count;
    draft->name[draft->cursor] = kCharacters[next];
  };
  rows[2].label = "POSITION";
  rows[2].value = [draft] {
    return std::to_string(draft->cursor + 1) + " OF " + std::to_string(kMaxGamertag);
  };
  rows[2].adjust = [draft](int direction) {
    const size_t last = std::min(draft->name.size(), kMaxGamertag - 1);
    draft->cursor = direction > 0 ? std::min(draft->cursor + 1, last)
                                  : (draft->cursor ? draft->cursor - 1 : 0);
  };
  rows[3].label = "DELETE LETTER";
  rows[3].activate = [draft] {
    if (draft->cursor < draft->name.size()) {
      draft->name.erase(draft->cursor, 1);
    } else if (!draft->name.empty()) {
      draft->name.pop_back();
      draft->cursor = draft->name.size();
    }
  };
  rows[4].label = "SAVE";
  rows[4].restart_required = true;
  rows[4].restart_pending = [this] {
    return !SameValue(Unquote(Saved("user_name")), Live("user_name"));
  };
  rows[4].activate = [this, draft] {
    config_.Set("user_name", config::Quote(draft->name));
    Save();
  };
  return std::make_unique<MenuScreen>("GAMERTAG", std::move(rows), [this] {
    return SameValue(Unquote(Saved("user_name")), Live("user_name"))
               ? std::string("LETTERS, DIGITS AND SPACES, UP TO 15")
               : std::string("RESTART THE GAME TO USE THE NEW GAMERTAG");
  });
}

std::unique_ptr<MenuScreen> SettingsPages::Profile() {
  std::vector<MenuRow> rows(2);
  rows[0].label = "GAMERTAG";
  rows[0].value = [this] { return Unquote(Saved("user_name")); };
  rows[0].activate = [this] { host_ui_.Push(Gamertag()); };
  rows[0].restart_required = true;
  rows[0].restart_pending = [this] {
    return !SameValue(Unquote(Saved("user_name")), Live("user_name"));
  };
  // FH1 picks its string table from the console language and country; each
  // pair was checked to load its table (probe of 2026-09-29).
  const std::tuple<const char*, int, int> languages[] = {
      {"ENGLISH (US)", 1, 103},        {"ENGLISH (UK)", 1, 35},
      {"FRENCH", 4, 34},               {"GERMAN", 3, 24},
      {"ITALIAN", 6, 50},              {"SPANISH (SPAIN)", 5, 31},
      {"SPANISH (MEXICO)", 5, 71},     {"PORTUGUESE (BRAZIL)", 9, 13},
      {"DUTCH", 16, 74},               {"DANISH", 1, 25},
      {"NORWEGIAN", 15, 75},           {"SWEDISH", 13, 90},
      {"FINNISH", 1, 32},              {"POLISH", 11, 82},
      {"CZECH", 1, 23},                {"HUNGARIAN", 1, 42},
      {"RUSSIAN", 12, 88},             {"JAPANESE", 2, 53},
      {"KOREAN", 7, 56},               {"CHINESE (TRADITIONAL)", 8, 101},
  };
  std::vector<Choice> choices;
  for (const auto& [label, language, country] : languages) {
    choices.push_back({label,
                       {{"user_language", std::to_string(language)},
                        {"user_country", std::to_string(country)}}});
  }
  rows[1] = Setting("LANGUAGE", std::move(choices), true);
  if (services_.save_backups) {
    MenuRow backups;
    backups.label = "SAVE BACKUPS";
    backups.activate = [this] { host_ui_.Push(Backups()); };
    rows.push_back(std::move(backups));
  }
  return std::make_unique<MenuScreen>("PROFILE", std::move(rows));
}

// "20260929T074240Z-session" as "2026-09-29 07:42 SESSION START".
std::string SlotLabel(const std::string& name) {
  if (name.size() < 16) {
    return Upper(name);
  }
  std::string label = name.substr(0, 4) + "-" + name.substr(4, 2) + "-" + name.substr(6, 2) +
                      " " + name.substr(9, 2) + ":" + name.substr(11, 2);
  const std::string reason = name.size() > 17 ? name.substr(17) : std::string();
  if (reason == "session") {
    label += " SESSION START";
  } else if (reason == "before-restore") {
    label += " BEFORE RESTORE";
  }
  return label;
}

std::unique_ptr<MenuScreen> SettingsPages::Backups() {
  SaveBackups* backups = services_.save_backups;
  std::vector<MenuRow> rows;
  if (backups->RestorePending()) {
    MenuRow cancel;
    cancel.label = "CANCEL THE RESTORE";
    cancel.activate = [backups] { backups->CancelRestore(); };
    rows.push_back(std::move(cancel));
  }
  for (const auto& slot : backups->List()) {
    MenuRow row;
    row.label = SlotLabel(slot.name);
    row.value = [kilobytes = slot.bytes >> 10] { return std::to_string(kilobytes) + " KB"; };
    row.activate = [this, name = slot.name] { host_ui_.Push(ConfirmRestore(name)); };
    rows.push_back(std::move(row));
  }
  const bool empty = rows.empty();
  return std::make_unique<MenuScreen>("SAVE BACKUPS", std::move(rows), [backups, empty] {
    if (backups->RestorePending()) {
      return std::string("RESTART THE GAME TO RESTORE THE CHOSEN BACKUP");
    }
    return std::string(empty ? "A BACKUP IS TAKEN AFTER EACH SAVE" : "NEWEST FIRST, UTC TIMES");
  });
}

std::unique_ptr<MenuScreen> SettingsPages::ConfirmRestore(std::string slot) {
  SaveBackups* backups = services_.save_backups;
  auto self = std::make_shared<const MenuScreen*>(nullptr);
  std::vector<MenuRow> rows(2);
  rows[0].label = "RESTORE AT NEXT START";
  rows[0].activate = [this, backups, slot, self] {
    backups->ScheduleRestore(slot);
    host_ui_.Finish(*self);
  };
  rows[1].label = "CANCEL";
  rows[1].activate = [this, self] { host_ui_.Finish(*self); };
  auto screen = std::make_unique<MenuScreen>("RESTORE BACKUP", std::move(rows));
  screen->set_body("The game will restore the saves from " + SlotLabel(slot) +
                   " when it next starts. Your current saves are backed up first, so this can "
                   "be undone.");
  screen->SetFocus(1);
  *self = screen.get();
  return screen;
}

std::unique_ptr<MenuScreen> SettingsPages::Root() {
  std::vector<MenuRow> rows;
  MenuRow resume;
  resume.label = "RESUME";
  resume.activate = [self = shared_from_this()] { self->host_ui_.Close(); };
  rows.push_back(std::move(resume));
  rows.push_back(Page("DISPLAY", &SettingsPages::Display));
  rows.push_back(Page("GRAPHICS", &SettingsPages::Graphics));
  rows.push_back(Page("AUDIO", &SettingsPages::Audio));
  rows.push_back(Page("CONTROLS", &SettingsPages::Controls));
  rows.push_back(Page("PROFILE", &SettingsPages::Profile));
  if (!services_.mods_root.empty()) {
    MenuRow row;
    row.label = "MODS";
    row.activate = [self = shared_from_this()] { self->host_ui_.Push(self->Mods()); };
    rows.push_back(std::move(row));
  }
  if (services_.achievements) {
    MenuRow row;
    row.label = "ACHIEVEMENTS";
    row.activate = [self = shared_from_this()] {
      self->host_ui_.Push(self->services_.achievements());
    };
    rows.push_back(std::move(row));
  }
  return std::make_unique<MenuScreen>("SETTINGS", std::move(rows));
}

}  // namespace

std::unique_ptr<hostui::MenuScreen> CreateSettingsMenu(hostui::HostUi& host_ui,
                                                       config::HostConfig& config,
                                                       SettingsServices services) {
  if (!config.Load()) {
    REXLOG_ERROR("Settings: cannot read {}; changes will not be saved", config.path().string());
  }
  return std::make_shared<SettingsPages>(host_ui, config, std::move(services))->Root();
}

void ApplyMasterVolume() {
  const int volume = std::clamp(REXCVAR_GET(pinyon_shift_master_volume), 0, 100);
  // Squared so equal steps sound roughly even.
  const float linear = float(volume) / 100.0f;
  rex::audio::SetOutputGain(linear * linear);
}

}  // namespace pinyon_shift::ui
