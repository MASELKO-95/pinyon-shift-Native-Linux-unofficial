#include "pinyon_shift_app.h"
#include "pinyon_shift_init.h"
#include "fh1_render_test.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <system_error>

#include <rex/cvar.h>
#include <rex/kernel/xboxkrnl/io.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>
#include <rex/input/input_system.h>
#include <rex/ui/flags.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app_context.h>
#include <rex/ui/window.h>

#include "native_renderer/guest_output_renderer.h"
#include "native_renderer/shader_capture.h"
#include "pinyon_shift_diagnostics.h"
#include "pinyon_shift_runtime_hooks.h"
#include "config/host_config.h"
#include "ui/host_style.h"
#include "ui/hostui/host_ui.h"
#include "ui/photo_export.h"
#include "ui/settings_menu.h"

#include <cstdio>

#ifdef PINYON_SHIFT_PGO_GENERATE
extern "C" int __llvm_profile_dump(void);
#endif

REXCVAR_DEFINE_UINT32(pinyon_shift_config_schema, 26, "Pinyon Shift",
                      "Pinyon Shift host configuration schema version");
REXCVAR_DEFINE_BOOL(pinyon_shift_capture_performance, true, "Pinyon Shift",
                    "Capture lightweight per-frame performance counters to a session CSV");
namespace {

// Schema 22 added the renderer choice (fh1_renderer) and schema 23 made the
// native renderer its default. Schema 24 retires the choice: the native
// renderer is the only renderer, so migration drops fh1_renderer and the other
// renderer-era settings the runtime no longer registers. Schema 25 drops the
// occlusion-query mode and ZPD classification settings: the host-query path
// is the only occlusion path. Schema 26 turns clear_memory_page_state off: it
// made every frame upload again every page the CPU had uploaded, about 20 MB
// of vertex data per race frame, and the race window runs 12 % faster
// without it with no rendering difference on the race, photo, dealership and
// FMV routes.
constexpr uint32_t kConfigSchema = 26;

bool EnsureSupportedConfig(const std::filesystem::path& path, bool& created,
                           bool& migrated) {
  created = false;
  migrated = false;
  if (!std::filesystem::exists(path)) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }
    output << "# Pinyon Shift host configuration.\n"
              "# Increment this only with an explicit migration.\n"
              "# Controller support with keyboard emulation as a fallback.\n"
              "pinyon_shift_config_schema = "
           << kConfigSchema << "\n"
              "input_backend = \"sdl\"\n"
              "hid_mappings_file = \"gamecontrollerdb.txt\"\n"
              "mnk_mode = true\n"
              "keybind_a = \"LMB,Space\"\n"
              "keybind_start = \"Return\"\n"
              "d3d12_allow_variable_refresh_rate_and_tearing = false\n"
              "vsync = true\n"
              "host_present_fps_limit = 0\n"
              "host_present_sleep_spin = true\n"
              "pinyon_shift_capture_performance = true\n"
              "xma_relaxed_padding_admission = false\n"
              "pinyon_shift_stabilize_vehicle_presentation = false\n"
              "pinyon_shift_skip_opening_movies = false\n"
              "pinyon_shift_fh1_render_fps_limit = 0\n"
              "pinyon_shift_fh1_source_presentation = true\n"
              "anisotropic_override = 3\n"
              "swap_post_effect = \"none\"\n"
              "disable_motion_blur = false\n"
              "disable_depth_of_field = false\n"
              "draw_resolution_scale_x = 1\n"
              "draw_resolution_scale_y = 1\n"
              "clear_memory_page_state = false\n";
    created = true;
    return output.good();
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  input.close();
  const std::string config_text = contents.str();
  const std::regex schema_pattern(
      R"((?:^|\n)\s*pinyon_shift_config_schema\s*=\s*([0-9]+)\s*(?:#.*)?(?:\r?\n|$))");
  std::smatch match;
  if (!std::regex_search(config_text, match, schema_pattern) || match.size() != 2) {
    return false;
  }
  try {
    const uint32_t schema = std::stoul(match[1].str());
    if (schema == kConfigSchema) {
      return true;
    }
    if (schema < 1 || schema >= kConfigSchema) {
      return false;
    }

    std::filesystem::path backup = path;
    backup += L".schema" + std::to_wstring(schema) + L".bak";
    std::error_code backup_error;
    std::filesystem::copy_file(path, backup,
                               std::filesystem::copy_options::skip_existing,
                               backup_error);
    if (backup_error && backup_error != std::errc::file_exists) {
      return false;
    }

    std::string migrated_text = config_text;
    migrated_text.replace(static_cast<size_t>(match.position(1)),
                          static_cast<size_t>(match.length(1)),
                          std::to_string(kConfigSchema));
    const std::regex rejected_interpolation_pattern(
        R"((?:^|\n)\s*pinyon_shift_fh1_frame_interpolation\s*=\s*(?:true|false)\s*(?:#.*)?(?:\r?\n|$))",
        std::regex::icase);
    migrated_text = std::regex_replace(migrated_text,
                                       rejected_interpolation_pattern, "\n");
    migrated_text = std::regex_replace(
        migrated_text,
        std::regex(R"((host_present_fps_limit\s*=\s*)\d+)",
                   std::regex::icase),
        "$1 0");
    migrated_text = std::regex_replace(
        migrated_text,
        std::regex(
            R"((?:^|\n)\s*pinyon_shift_fh1_guest_vblank_hz\s*=\s*\d+\s*(?:#.*)?(?:\r?\n|$))",
            std::regex::icase),
        "\n");
    migrated_text = std::regex_replace(
        migrated_text,
        std::regex(
            R"((?:^|\n)\s*pinyon_shift_native_renderer_texture_bridge\s*=\s*(?:true|false)\s*(?:#.*)?(?:\r?\n|$))",
            std::regex::icase),
        "\n");
    for (const char* retired_setting : {
             "pinyon_shift_native_renderer",
             "pinyon_shift_native_renderer_sky_horizon_suppression",
             "pinyon_shift_fh1_native_v4",
             "readback_resolve_half_pixel_offset",
             "readback_memexport",
             "readback_memexport_fast",
             "pinyon_shift_native_renderer_census",
             // Schema 24: renderer-selection, native-shadow and Xenos-era
             // renderer settings that no longer exist.
             "fh1_renderer",
             "fh1_native_shadow",
             "fh1_native_shadow_dump_dir",
             "fh1_native_shadow_dump_frames",
             "fh1_native_shadow_verify",
             "fh1_native_shadow_verify_draws",
             "fh1_discovery_sampling",
             "fh1_owned_depth_clear",
             "fh1_owned_depth_tile_clear",
             "fh1_native_reflection_mips",
             "fh1_mip_decode_probe",
             "fh1_native_ui_boundary_probe",
             "fh1_glow_probe",
             "fh1_recycle_geometry_buffers",
             "fh1_contain_geometry_windows",
             "fh1_cache_geometry_rejections",
             "fh1_geometry_cache_mb",
             "native_stencil_value_output",
             "native_stencil_value_output_d3d12_intel",
             "pinyon_shift_native_race",
             "pinyon_shift_native_race_capture_start_frame",
             "pinyon_shift_native_ui_live",
             "pinyon_shift_native_ui_replay_source_frame",
             "pinyon_shift_native_ui_scene_probe",
             "pinyon_shift_native_ui_shadow_start_frame",
             "pinyon_shift_native_ui_clear_probe",
             "pinyon_shift_native_output_clear_probe",
             "pinyon_shift_native_scene_clear_probe",
             "pinyon_shift_native_scene_triangle_probe",
             "pinyon_shift_native_small_target_probe",
             "pinyon_shift_native_track_probe",
             "pinyon_shift_native_ordered_live_probe",
             "pinyon_shift_fh1_clear_producer_trace",
             "pinyon_shift_fh1_scene_dump",
             "pinyon_shift_snr01_trace_following_frame",
             "pinyon_shift_snr01_trace_resident_packet_writers",
             "pinyon_shift_snr01_trace_source_frame",
             "pinyon_shift_snr01_watch_packet_pages",
             "pinyon_shift_snr02_item_payload_probe",
             "pinyon_shift_snr02_trace_first_rebuild_after_frame",
             "pinyon_shift_snr02_trace_view_call",
             "pinyon_shift_snr02_track_payload_probe",
             "pinyon_shift_snr03_probe_following_frame",
             "pinyon_shift_snr03_probe_frame",
             "pinyon_shift_snr04_live_continuous",
             "pinyon_shift_snr04_live_handoff",
             "pinyon_shift_snr04_live_source_frame",
             "pinyon_shift_snr04_live_worker",
             "pinyon_shift_snr_m02_trace_source_frame",
             // Schema 25: one occlusion-query path.
             "occlusion_query",
             "zpd_end_policy",
             "zpd_end_fallback"}) {
      migrated_text = std::regex_replace(
          migrated_text,
          std::regex("(?:^|\\n)\\s*" + std::string(retired_setting) +
                         "\\s*=.*(?:\\r?\\n|$)",
                     std::regex::icase),
          "\n");
    }
    // readback_resolve stays a developer setting (none, fast, some, full), but
    // launchers before schema 24 wrote it for players, and `fast` never
    // reaches free roam: drop those values only.
    if (schema < 24) {
      migrated_text = std::regex_replace(
          migrated_text,
          std::regex(R"((?:^|\n)\s*readback_resolve\s*=.*(?:\r?\n|$))", std::regex::icase),
          "\n");
    }
    if (schema < 26) {
      migrated_text = std::regex_replace(
          migrated_text,
          std::regex(R"((^|\n)(\s*clear_memory_page_state\s*=\s*)true)", std::regex::icase),
          "$1$2false");
    }
    if (schema == 1) {
      const std::regex stabilization_pattern(
          R"((?:^|\n)\s*pinyon_shift_stabilize_vehicle_presentation\s*=\s*(true|false)\s*(?:#.*)?(?:\r?\n|$))");
      std::smatch stabilization_match;
      if (std::regex_search(migrated_text, stabilization_match,
                            stabilization_pattern)) {
        migrated_text.replace(
            static_cast<size_t>(stabilization_match.position(1)),
            static_cast<size_t>(stabilization_match.length(1)), "false");
      } else {
        if (!migrated_text.empty() && migrated_text.back() != '\n') {
          migrated_text.push_back('\n');
        }
        migrated_text +=
            "pinyon_shift_stabilize_vehicle_presentation = false\n";
      }
    }

    const std::regex accept_binding_pattern(
        R"((?:^|\n)\s*keybind_a\s*=\s*\"[^\"]*\"\s*(?:#.*)?(?:\r?\n|$))");
    if (!std::regex_search(migrated_text, accept_binding_pattern)) {
      if (!migrated_text.empty() && migrated_text.back() != '\n') {
        migrated_text.push_back('\n');
      }
      migrated_text += "keybind_a = \"LMB,Space\"\n";
    }

    const std::pair<const char*, const char*> graphics_settings[] = {
        {"xma_relaxed_padding_admission",
         "xma_relaxed_padding_admission = false\n"},
        {"anisotropic_override", "anisotropic_override = 3\n"},
        {"swap_post_effect", "swap_post_effect = \"none\"\n"},
        {"disable_motion_blur", "disable_motion_blur = false\n"},
        {"disable_depth_of_field", "disable_depth_of_field = false\n"},
        {"draw_resolution_scale_x", "draw_resolution_scale_x = 1\n"},
        {"draw_resolution_scale_y", "draw_resolution_scale_y = 1\n"},
        {"vsync", "vsync = true\n"},
        {"host_present_fps_limit", "host_present_fps_limit = 0\n"},
        {"host_present_sleep_spin", "host_present_sleep_spin = true\n"},
        {"clear_memory_page_state", "clear_memory_page_state = false\n"},
        {"pinyon_shift_fh1_render_fps_limit",
         "pinyon_shift_fh1_render_fps_limit = 0\n"},
        {"pinyon_shift_fh1_source_presentation",
         "pinyon_shift_fh1_source_presentation = true\n"},
    };
    for (const auto& [name, line] : graphics_settings) {
      const std::regex setting_pattern("(?:^|\\n)\\s*" + std::string(name) +
                                       "\\s*=", std::regex::icase);
      if (!std::regex_search(migrated_text, setting_pattern)) {
        if (!migrated_text.empty() && migrated_text.back() != '\n') {
          migrated_text.push_back('\n');
        }
        migrated_text += line;
      }
    }

    std::filesystem::path temporary = path;
    temporary += L".migrating";
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output) {
        return false;
      }
      output << migrated_text;
      if (!output.good()) {
        return false;
      }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
      std::error_code error;
      std::filesystem::remove(temporary, error);
      return false;
    }
    migrated = true;
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

}  // namespace

std::unique_ptr<rex::ui::WindowedApp> PinyonShiftApp::Create(
    rex::ui::WindowedAppContext& context) {
  if (!pinyon_shift::diagnostics::InitializeEarly()) {
    ExitProcess(ERROR_NOT_SUPPORTED);
  }
  return std::unique_ptr<PinyonShiftApp>(
      new PinyonShiftApp(context, "pinyon_shift", PPCImageConfig));
}

void PinyonShiftApp::OnConfigurePaths(rex::PathConfig& paths) {
  namespace diagnostics = pinyon_shift::diagnostics;
  const auto& state_root = diagnostics::StateRoot();
  if (auto game_root = diagnostics::EnvironmentPath("PINYON_SHIFT_GAME_ROOT")) {
    paths.game_data_root = *game_root;
  }
  paths.user_data_root = state_root / "user";
  paths.update_data_root = state_root / "update";
  paths.cache_root = state_root / "cache";
  paths.config_path = state_root / "config" / "pinyon_shift.toml";
  host_config_ = std::make_unique<pinyon_shift::config::HostConfig>(paths.config_path);

  bool config_created = false;
  bool config_migrated = false;
  if (!EnsureSupportedConfig(paths.config_path, config_created,
                             config_migrated)) {
    diagnostics::RecordEvent("config.unsupported",
                             {{"path", paths.config_path.string()},
                              {"required_schema", std::to_string(kConfigSchema)}});
    MessageBoxW(nullptr,
                L"Pinyon Shift could not create the host configuration, or its schema is "
                L"unsupported. Remove or migrate pinyon_shift.toml before retrying.",
                L"Unsupported configuration", MB_OK | MB_ICONERROR);
    ExitProcess(ERROR_REVISION_MISMATCH);
  }

  if (REXCVAR_GET(log_file).empty()) {
    REXCVAR_SET(log_file, (state_root / "logs" / "runtime.log").string());
  }

  diagnostics::RecordEvent(
      "paths.configured",
      {{"game", paths.game_data_root.string()},
       {"user", paths.user_data_root.string()},
       {"update", paths.update_data_root.string()},
       {"cache", paths.cache_root.string()},
       {"config", paths.config_path.string()},
       {"config_schema", std::to_string(kConfigSchema)},
       {"config_created", config_created ? "1" : "0"},
       {"config_migrated", config_migrated ? "1" : "0"},
       {"log", REXCVAR_GET(log_file)}});
}

std::optional<rex::PathConfig> PinyonShiftApp::OnFinalizePaths(
    const rex::PathConfig& defaults,
    std::function<void(rex::PathConfig)> resume) {
  (void)resume;
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  MONITORINFOEXW monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  const HWND hwnd = window() ? static_cast<HWND>(window()->GetNativeWindowHandle())
                             : nullptr;
  const HMONITOR monitor =
      hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
  if (monitor && GetMonitorInfoW(monitor, &monitor_info) &&
      EnumDisplaySettingsW(monitor_info.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
      mode.dmDisplayFrequency >= 24 && mode.dmDisplayFrequency <= 240) {
    REXCVAR_SET(video_mode_refresh_rate, double(mode.dmDisplayFrequency));
    pinyon_shift::diagnostics::RecordEvent(
        "display.refresh.detected",
        {{"hz", std::to_string(mode.dmDisplayFrequency)}});
  }
  return defaults;
}

void PinyonShiftApp::OnConfigureFonts(ImFontAtlas* atlas) {
  float dpi_scale = 1.0f;
  if (const rex::ui::Window* host_window = window()) {
    dpi_scale = float(host_window->GetDpi()) / float(host_window->GetMediumDpi());
  }
  pinyon_shift::ui::ConfigureHostFonts(atlas, dpi_scale);
}

void PinyonShiftApp::OnConfigureStyle(ImGuiStyle& imgui_style, rex::ui::Style& ui_style) {
  pinyon_shift::ui::ConfigureHostStyle(imgui_style, ui_style);
}

void PinyonShiftApp::OnPostInitLogging() {
  std::string perf_csv = rex::cvar::GetFlagByName("perf_log_csv");
  if (perf_csv.empty() && REXCVAR_GET(pinyon_shift_capture_performance)) {
    perf_csv = (pinyon_shift::diagnostics::StateRoot() / "logs" /
                (pinyon_shift::diagnostics::SessionId() + ".perf.csv"))
                   .string();
  }
  if (!perf_csv.empty()) {
    rex::perf::SetCsvLogPath(perf_csv);
  }
  pinyon_shift::diagnostics::RecordEvent(
      "logging.ready", {{"config_schema", std::to_string(REXCVAR_GET(pinyon_shift_config_schema))},
                        {"d3d12_tearing_allowed",
                         rex::cvar::GetFlagByName(
                             "d3d12_allow_variable_refresh_rate_and_tearing")},
                        {"vehicle_presentation_stabilization",
                         rex::cvar::GetFlagByName(
                             "pinyon_shift_stabilize_vehicle_presentation")},
                        {"renderer", "d3d12"},
                        {"resolution", rex::cvar::GetFlagByName("resolution")},
                        {"vsync", rex::cvar::GetFlagByName("vsync")},
                        {"host_present_fps_limit",
                         rex::cvar::GetFlagByName("host_present_fps_limit")},
                        {"host_present_sleep_spin",
                         rex::cvar::GetFlagByName("host_present_sleep_spin")},
                        {"draw_resolution_scale_x",
                         rex::cvar::GetFlagByName("draw_resolution_scale_x")},
                        {"draw_resolution_scale_y",
                         rex::cvar::GetFlagByName("draw_resolution_scale_y")},
                        {"clear_memory_page_state",
                         rex::cvar::GetFlagByName("clear_memory_page_state")},
                        {"anisotropic_override",
                         rex::cvar::GetFlagByName("anisotropic_override")},
                        {"swap_post_effect", rex::cvar::GetFlagByName("swap_post_effect")},
                        {"disable_motion_blur",
                         rex::cvar::GetFlagByName("disable_motion_blur")},
                        {"disable_depth_of_field",
                         rex::cvar::GetFlagByName("disable_depth_of_field")},
                        {"fh1_gpu_corpus",
                         rex::cvar::GetFlagByName(
                             "pinyon_shift_fh1_gpu_corpus")},
                        {"fh1_render_fps_limit",
                         rex::cvar::GetFlagByName(
                             "pinyon_shift_fh1_render_fps_limit")},
                        {"fh1_source_presentation",
                         rex::cvar::GetFlagByName(
                             "pinyon_shift_fh1_source_presentation")},
                        {"xma_relaxed_padding_admission",
                         rex::cvar::GetFlagByName(
                             "xma_relaxed_padding_admission")},
                        {"perf_csv_enabled", perf_csv.empty() ? "0" : "1"},
                        {"perf_csv", perf_csv}});
}

void PinyonShiftApp::OnPreSetup(rex::RuntimeConfig& config) {
  config.gpu_plugin =
      GetEnvironmentVariableW(L"PINYON_SHIFT_FH1_DISC_SHADER_CORPUS_DIR", nullptr, 0)
          ? "fh1-producer"
          : "fh1";
  pinyon_shift::fh1_render_test::Configure(config);
  pinyon_shift::diagnostics::RecordEvent(
      "runtime.setup.begin",
      {{"graphics_requested", (config.graphics || !config.gpu_plugin.empty()) ? "1" : "0"},
       {"gpu_plugin", config.gpu_plugin},
       {"audio_requested", config.audio_factory ? "1" : "0"},
       {"input_requested", config.input_factory ? "1" : "0"}});
}

void PinyonShiftApp::OnPostLoadXexImage() {
  const auto title_id = runtime() && runtime()->kernel_state()
                            ? runtime()->kernel_state()->title_id()
                            : 0;
  char title[16]{};
  std::snprintf(title, sizeof(title), "%08X", title_id);
  pinyon_shift::diagnostics::RecordEvent("xex.loaded", {{"title_id", title}});
}

PinyonShiftApp::~PinyonShiftApp() = default;

void PinyonShiftApp::ToggleGameMenu() {
  if (host_ui_ && host_ui_->is_open()) {
    host_ui_->Close();
    return;
  }
  OpenSettingsMenu();
}

void PinyonShiftApp::OpenSettingsMenu() {
  if (host_ui_ && host_ui_->is_open()) {
    return;
  }
  if (!host_ui_) {
    rex::ui::Presenter* presenter =
        runtime() && runtime()->graphics_system() ? runtime()->graphics_system()->presenter()
                                                  : nullptr;
    if (!presenter || !immediate_drawer() || !window()) {
      REXLOG_WARN("Host UI: presentation is not ready");
      return;
    }
    host_ui_ = std::make_unique<pinyon_shift::hostui::HostUi>(
        *this, *presenter, *immediate_drawer(), *window(),
        static_cast<rex::input::InputSystem*>(runtime()->input_system()), game_data_root());
  }
  if (!host_config_) {
    REXLOG_WARN("Host UI: the settings file is not known yet");
    return;
  }
  host_ui_->Open(pinyon_shift::ui::CreateSettingsMenu(*host_ui_, *host_config_));
}

void PinyonShiftApp::OnPostSetup() {
  rex::ui::RegisterBind("bind_game_menu", "F6", "Open the in-game settings menu",
                        [this] { ToggleGameMenu(); });
  rex::ui::RegisterBind("bind_photo", "F8", "Save the current frame as a PNG photo", [this] {
    pinyon_shift::ui::SavePhoto(runtime() && runtime()->graphics_system()
                                    ? runtime()->graphics_system()->presenter()
                                    : nullptr);
  });
  rex::ui::RegisterBind("bind_fullscreen", "F11", "Toggle fullscreen", [this] {
    const bool fullscreen = !REXCVAR_GET(fullscreen);
    rex::cvar::SetFlagByName("fullscreen", fullscreen ? "true" : "false");
    if (host_config_ && host_config_->Load()) {
      host_config_->Set("fullscreen", fullscreen ? "true" : "false");
      host_config_->Save();
    }
  });
  // ResizeBuffers cannot toggle tearing: a changed preference makes the
  // presenter recreate its swap chain on the next surface update, which is
  // requested outside any drawing.
  rex::cvar::RegisterChangeCallback(
      "d3d12_allow_variable_refresh_rate_and_tearing", [this](std::string_view, std::string_view) {
        if (!window()) {
          return;
        }
        window()->app_context().CallInUIThreadDeferred([this] {
          if (runtime() && runtime()->graphics_system() &&
              runtime()->graphics_system()->presenter()) {
            runtime()->graphics_system()->presenter()->OnSurfaceResizeFromUIThread();
          }
        });
      });
  // SETTINGS in the pause menu (NP-1.5): the hook runs on the guest thread.
  PinyonShiftSetPauseSettingsHandler([this] {
    if (window()) {
      window()->app_context().CallInUIThreadDeferred([this] { OpenSettingsMenu(); });
    }
  });
  pinyon_shift::ui::ApplyMasterVolume();
  rex::cvar::RegisterChangeCallback(
      "pinyon_shift_master_volume",
      [](std::string_view, std::string_view) { pinyon_shift::ui::ApplyMasterVolume(); });
  pinyon_shift::diagnostics::RefreshCrashReporter();
  rex::kernel::xboxkrnl::SetGuestFileOpenObserver(&PinyonShiftObserveGuestFileOpen);
  pinyon_shift::native_renderer::InstallGuestOutputRenderer(
      runtime() ? runtime()->graphics_system() : nullptr);
  pinyon_shift::native_renderer::InstallShaderCapture(
      runtime() ? runtime()->graphics_system() : nullptr);
  pinyon_shift::diagnostics::RecordEvent(
      "runtime.setup.complete",
      {{"memory", runtime() && runtime()->memory() ? "1" : "0"},
       {"vfs", runtime() && runtime()->file_system() ? "1" : "0"},
       {"kernel", runtime() && runtime()->kernel_state() ? "1" : "0"},
       {"graphics", runtime() && runtime()->graphics_system() ? "1" : "0"},
       {"audio", runtime() && runtime()->audio_system() ? "1" : "0"},
       {"input", runtime() && runtime()->input_system() ? "1" : "0"}});
}

void PinyonShiftApp::OnPreLaunchModule() {
  pinyon_shift::diagnostics::RefreshCrashReporter();
  pinyon_shift::diagnostics::RecordEvent("guest.launch.begin");
}

void PinyonShiftApp::OnPostLaunchModule(rex::system::XThread* thread) {
  const std::string thread_id = thread ? std::to_string(thread->thread_id()) : "none";
  pinyon_shift::diagnostics::RecordEvent("guest.thread.prepared", {{"thread_id", thread_id}});
  pinyon_shift::fh1_render_test::Start(
      runtime() ? runtime()->graphics_system() : nullptr, &app_context(),
      window(), [this] { OnWindowCloseRequested(); });
}

bool PinyonShiftApp::ShouldStartModuleThread() {
  // A frame replay drives the GPU on its own; the title stays suspended.
  return rex::cvar::GetFlagByName("fh1_frame_replay").empty();
}

void PinyonShiftApp::OnGuestThreadExit(rex::system::XThread* thread) {
  const std::string thread_id = thread ? std::to_string(thread->thread_id()) : "none";
  pinyon_shift::diagnostics::RecordEvent("guest.thread.exit", {{"thread_id", thread_id}});
}

bool PinyonShiftApp::OnWindowCloseRequested() {
  // ReXGlue 0.9 deliberately hard-exits after accepting a window-close
  // request, so OnDestroy/OnShutdown are not reached on that path. Record the
  // clean qualification boundary before allowing the SDK to terminate.
  pinyon_shift::native_renderer::UninstallShaderCapture(
      runtime() ? runtime()->graphics_system() : nullptr);
  RecordShutdownOnce();
#ifdef PINYON_SHIFT_PGO_GENERATE
  // The SDK's hard exit skips the executable's profile atexit handler.
  const int profile_result = __llvm_profile_dump();
  pinyon_shift::diagnostics::RecordEvent(
      "pgo.profile.dump", {{"result", std::to_string(profile_result)}});
#endif
  return true;
}

void PinyonShiftApp::OnShutdown() {
  rex::ui::UnregisterBind("bind_game_menu");
  rex::ui::UnregisterBind("bind_fullscreen");
  rex::ui::UnregisterBind("bind_photo");
  pinyon_shift::ui::WaitForPhoto();
  PinyonShiftSetPauseSettingsHandler(nullptr);
  rex::cvar::UnregisterChangeCallbacks("d3d12_allow_variable_refresh_rate_and_tearing");
  // Before the presenter, drawer and kernel it uses are torn down.
  host_ui_.reset();
  pinyon_shift::fh1_render_test::Stop();
  pinyon_shift::native_renderer::UninstallShaderCapture(
      runtime() ? runtime()->graphics_system() : nullptr);
  pinyon_shift::native_renderer::UninstallGuestOutputRenderer(
      runtime() ? runtime()->graphics_system() : nullptr);
  RecordShutdownOnce();
}

void PinyonShiftApp::RecordShutdownOnce() {
  if (!shutdown_recorded_.exchange(true, std::memory_order_acq_rel)) {
    pinyon_shift::diagnostics::RecordEvent("process.shutdown");
  }
}
