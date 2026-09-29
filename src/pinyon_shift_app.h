#pragma once

#include <atomic>
#include <memory>

#include <rex/rex_app.h>

namespace pinyon_shift::config {
class HostConfig;
}
namespace pinyon_shift::hostui {
class HostUi;
}

class PinyonShiftApp final : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  ~PinyonShiftApp() override;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& context);

 protected:
  void OnConfigurePaths(rex::PathConfig& paths) override;
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override;
  void OnPostInitLogging() override;
  void OnConfigureFonts(ImFontAtlas* atlas) override;
  void OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) override;
  void OnPreSetup(rex::RuntimeConfig& config) override;
  void OnPostLoadXexImage() override;
  void OnPostSetup() override;
  void OnPreLaunchModule() override;
  void OnPostLaunchModule(rex::system::XThread* thread) override;
  bool ShouldStartModuleThread() override;
  void OnGuestThreadExit(rex::system::XThread* thread) override;
  bool OnWindowCloseRequested() override;
  void OnShutdown() override;

 private:
  void RecordShutdownOnce();
  void ToggleGameMenu();

  std::atomic_bool shutdown_recorded_{false};
  // Created on first use: the presenter and input system it needs exist only
  // after runtime setup.
  std::unique_ptr<pinyon_shift::hostui::HostUi> host_ui_;
  // The settings file the in-game settings screen edits.
  std::unique_ptr<pinyon_shift::config::HostConfig> host_config_;
};
