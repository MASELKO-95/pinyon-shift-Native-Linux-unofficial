#include "ui/xam_dialogs.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/logging.h>

#include "pinyon_shift_diagnostics.h"
#include "ui/hostui/host_ui.h"
#include "ui/hostui/menu.h"

namespace pinyon_shift::ui {
namespace {

using hostui::MenuRow;
using hostui::MenuScreen;
using rex::kernel::xam::XamUiProvider;

// The display face is drawn in capitals, like the title's own menus.
std::string Upper(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return c < 0x80 ? char(std::toupper(c)) : char(c);
  });
  return text;
}

// Calls `done` once, however the dialog ends.
template <typename... Args>
auto Once(std::function<void(Args...)> done) {
  auto called = std::make_shared<bool>(false);
  return [done = std::move(done), called](Args... args) {
    if (!*called) {
      *called = true;
      done(std::forward<Args>(args)...);
    }
  };
}

class XamDialogs final : public XamUiProvider {
 public:
  XamDialogs(hostui::HostUi& host_ui, std::function<std::unique_ptr<MenuScreen>()> achievements)
      : host_ui_(host_ui), achievements_(std::move(achievements)) {}

  void ShowAchievements(std::function<void()> done) override {
    auto finish = Once(std::move(done));
    auto screen = achievements_ ? achievements_() : nullptr;
    if (!screen) {
      finish();
      return;
    }
    screen->set_on_back([finish] { finish(); });
    diagnostics::RecordEvent("xam.dialog.open", {{"kind", "achievements"}});
    if (!host_ui_.OpenDialog(std::move(screen))) {
      finish();
    }
  }

  void ShowMessageBox(const std::string& title, const std::string& text,
                      const std::vector<std::string>& buttons, uint32_t default_button,
                      std::function<void(uint32_t)> done) override {
    auto finish = Once(std::move(done));
    auto self = std::make_shared<const MenuScreen*>(nullptr);
    std::vector<MenuRow> rows;
    for (size_t i = 0; i < std::max<size_t>(buttons.size(), 1); ++i) {
      MenuRow row;
      row.label = buttons.empty() ? std::string("OK") : Upper(buttons[i]);
      row.activate = [this, self, finish, i] {
        host_ui_.Finish(*self);
        finish(uint32_t(i));
      };
      rows.push_back(std::move(row));
    }
    auto screen =
        std::make_unique<MenuScreen>(Upper(title.empty() ? "Message" : title), std::move(rows));
    screen->set_body(text);
    screen->SetFocus(default_button);
    screen->set_on_back([finish] { finish(XamUiProvider::kCancelled); });
    *self = screen.get();
    diagnostics::RecordEvent("xam.dialog.open", {{"kind", "message_box"}, {"title", title}});
    if (!host_ui_.OpenDialog(std::move(screen))) {
      REXLOG_WARN("XAM message box: the host UI is unavailable; choosing the default button");
      finish(default_button);
    }
  }

  void ShowKeyboard(const std::string& title, const std::string& description,
                    const std::string& default_text, size_t max_length,
                    std::function<void(bool, std::string)> done) override {
    auto finish = Once(std::move(done));
    struct Draft {
      std::string text;
      size_t cursor = 0;
      size_t max_length = 0;
    };
    auto draft = std::make_shared<Draft>();
    draft->max_length = max_length ? max_length : 256;
    draft->text = default_text.substr(0, draft->max_length);
    draft->cursor = draft->text.size();
    static constexpr std::string_view kCharacters =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .,-_!?'";
    auto self = std::make_shared<const MenuScreen*>(nullptr);
    std::vector<MenuRow> rows(6);
    rows[0].label = "TEXT";
    rows[0].value = [draft] { return draft->text + "_"; };
    rows[0].enabled = [] { return false; };
    rows[1].label = "LETTER";
    rows[1].value = [draft] {
      return draft->cursor < draft->text.size() ? std::string(1, draft->text[draft->cursor])
                                                : std::string("+");
    };
    rows[1].adjust = [draft](int direction) {
      const size_t count = kCharacters.size();
      if (draft->cursor >= draft->text.size()) {
        if (draft->text.size() < draft->max_length) {
          draft->text.push_back(direction > 0 ? kCharacters.front() : kCharacters.back());
        }
        return;
      }
      const size_t index = kCharacters.find(draft->text[draft->cursor]);
      const size_t next = index == std::string_view::npos
                              ? 0
                              : (index + count + size_t(direction > 0 ? 1 : count - 1)) % count;
      draft->text[draft->cursor] = kCharacters[next];
    };
    rows[2].label = "POSITION";
    rows[2].value = [draft] { return std::to_string(draft->cursor + 1); };
    rows[2].adjust = [draft](int direction) {
      const size_t last = std::min(draft->text.size(), draft->max_length - 1);
      draft->cursor = direction > 0 ? std::min(draft->cursor + 1, last)
                                    : (draft->cursor ? draft->cursor - 1 : 0);
    };
    rows[3].label = "DELETE LETTER";
    rows[3].activate = [draft] {
      if (draft->cursor < draft->text.size()) {
        draft->text.erase(draft->cursor, 1);
      } else if (!draft->text.empty()) {
        draft->text.pop_back();
        draft->cursor = draft->text.size();
      }
    };
    rows[4].label = "DONE";
    rows[4].activate = [this, self, draft, finish] {
      host_ui_.Finish(*self);
      finish(true, draft->text);
    };
    rows[5].label = "CANCEL";
    rows[5].activate = [this, self, finish] {
      host_ui_.Finish(*self);
      finish(false, std::string());
    };
    const std::string heading = title.empty() ? description : title;
    auto screen =
        std::make_unique<MenuScreen>(Upper(heading.empty() ? "Enter text" : heading), std::move(rows));
    if (!title.empty() && !description.empty()) {
      screen->set_body(description);
    }
    screen->SetFocus(4);
    screen->set_on_back([finish] { finish(false, std::string()); });
    // Typing edits at the end, like a text field; backspace deletes.
    screen->set_text_input([draft](char32_t code_point) {
      if (code_point == U'\b') {
        if (!draft->text.empty()) {
          draft->text.pop_back();
        }
      } else if (code_point < 0x80 && draft->text.size() < draft->max_length) {
        draft->text.push_back(char(code_point));
      }
      draft->cursor = draft->text.size();
    });
    *self = screen.get();
    diagnostics::RecordEvent("xam.dialog.open", {{"kind", "keyboard"}, {"title", heading}});
    if (!host_ui_.OpenDialog(std::move(screen))) {
      REXLOG_WARN("XAM keyboard: the host UI is unavailable; returning the default text");
      finish(true, default_text);
    }
  }

 private:
  hostui::HostUi& host_ui_;
  std::function<std::unique_ptr<MenuScreen>()> achievements_;
};

}  // namespace

std::unique_ptr<XamUiProvider> CreateXamDialogs(
    hostui::HostUi& host_ui, std::function<std::unique_ptr<MenuScreen>()> achievements) {
  return std::make_unique<XamDialogs>(host_ui, std::move(achievements));
}

}  // namespace pinyon_shift::ui
