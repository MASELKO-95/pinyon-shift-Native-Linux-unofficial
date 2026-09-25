#include <rex/system/interfaces/graphics.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>

#include <chrono>
#include <atomic>
#include <mutex>

#include "fh1_render_test.h"
#include "native_renderer/graphics_hooks.h"
#include "native_renderer/guest_output_renderer.h"
#if defined(_WIN32)
#include "native_renderer/native_output_track.h"
#include "native_renderer/native_output_triangle.h"
#endif
#include "native_renderer/snr04_owned_scene_diagnostic.h"

REXCVAR_DEFINE_BOOL(pinyon_shift_native_output_clear_probe, false,
                    "Pinyon Shift",
                    "Exercise opt-in early native output selection during render tests")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(pinyon_shift_native_scene_clear_probe, false,
                    "Pinyon Shift",
                    "Claim render-test output only with a current owned race scene")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
#if defined(_WIN32)
REXCVAR_DEFINE_BOOL(pinyon_shift_native_scene_triangle_probe, false,
                    "Pinyon Shift",
                    "Exercise scene-gated D3D12 native graphics output")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(pinyon_shift_native_track_probe, false,
                    "Pinyon Shift",
                    "Draw owned track geometry into the native output")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(pinyon_shift_native_race, false, "Pinyon Shift",
                    "Experimental native race output (requires scene capture)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pinyon_shift_native_ui_clear_probe, false, "Pinyon Shift",
                    "Test guest HUD retention over a flat native race background")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(pinyon_shift_native_ui_scene_probe, false, "Pinyon Shift",
                    "Render the owned race scene before original guest HUD draws")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
#endif

namespace {

#if defined(_WIN32)
std::atomic_bool native_race_enabled{false};
#endif

bool ObserveRenderTestOutput(
    const rex::system::NativeGuestOutputRenderContext& context) {
  static thread_local uint64_t captured_frame = 0;
  static thread_local uint64_t capture_us = 0;
  const auto capture_scene = [&] {
    const auto capture_begin = std::chrono::steady_clock::now();
    pinyon_shift::native_renderer::ObserveSnr03OutputFrame(
        context.frame_sequence, context.device);
    pinyon_shift::native_renderer::ObserveSnr02ItemOutputFrame(
        context.frame_sequence, context.device);
    pinyon_shift::native_renderer::ObserveSnr02TrackOutputFrame(
        context.frame_sequence);
    captured_frame = context.frame_sequence;
    capture_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - capture_begin).count();
  };
  if (context.phase == rex::system::NativeGuestOutputPhase::kBeforeUi) {
#if defined(_WIN32)
    if (!native_race_enabled.load(std::memory_order_acquire))
      return false;
    const bool admitted =
        pinyon_shift::native_renderer::NativeRaceAdmittedForOutput(
            context.frame_sequence);
    rex::perf::TraceCriticalPath("native_admission_pre_ui",
                                 int64_t(context.frame_sequence) - 1, admitted);
    if (!admitted) return false;
    capture_scene();
    REXGPU_INFO("FH1 native pre-UI scene output_frame={} capture_us={}",
                captured_frame, capture_us);
    if (REXCVAR_GET(pinyon_shift_native_ui_clear_probe) ||
        REXCVAR_GET(pinyon_shift_native_ui_scene_probe)) {
      auto scene = pinyon_shift::native_renderer::SnapshotSnr04LiveScene(
          context.frame_sequence);
      if (scene && REXCVAR_GET(pinyon_shift_native_ui_clear_probe) &&
          context.clear_color) {
        const float sky[4]{0.11f, 0.22f, 0.43f, 1.f};
        return context.clear_color(context, sky);
      }
      if (scene && REXCVAR_GET(pinyon_shift_native_ui_scene_probe))
        return pinyon_shift::native_renderer::DrawNativeOutputTrack(
            context, *scene);
    }
#endif
    return false;
  }
  if (context.phase == rex::system::NativeGuestOutputPhase::kNativeAttempt) {
#if defined(_WIN32)
    if (native_race_enabled.load(std::memory_order_acquire))
      rex::perf::TraceCriticalPath(
          "native_admission_output", int64_t(context.frame_sequence) - 1,
          pinyon_shift::native_renderer::NativeRaceAdmittedForOutput(
              context.frame_sequence));
#endif
    if (captured_frame != context.frame_sequence) capture_scene();
#if defined(_WIN32)
    // A missed early boundary must keep the complete guest frame. The late
    // compositor cannot safely recover UI over a partially replaced target.
    if (REXCVAR_GET(pinyon_shift_native_ui_clear_probe) ||
        REXCVAR_GET(pinyon_shift_native_ui_scene_probe))
      return false;
    const bool native_race_requested =
        native_race_enabled.load(std::memory_order_acquire);
    const bool native_race_admitted =
        native_race_requested &&
        pinyon_shift::native_renderer::NativeRaceAdmittedForOutput(
            context.frame_sequence);
    if (native_race_requested) {
      static thread_local bool previous_admission = false;
      if (native_race_admitted != previous_admission) {
        REXGPU_INFO("FH1 native race admission output_frame={} admitted={}",
                    context.frame_sequence, native_race_admitted);
        previous_admission = native_race_admitted;
      }
    }
    if ((native_race_admitted ||
         (REXCVAR_GET(pinyon_shift_native_track_probe) &&
          pinyon_shift::fh1_render_test::Enabled())) &&
        context.guest_output_width == 1280 &&
        context.guest_output_height == 720) {
      auto scene = pinyon_shift::native_renderer::SnapshotSnr04LiveScene(
          context.frame_sequence);
      if (scene)
        return pinyon_shift::native_renderer::DrawNativeOutputTrack(
            context, *scene);
    }
    if (REXCVAR_GET(pinyon_shift_native_scene_triangle_probe) &&
        pinyon_shift::fh1_render_test::Enabled() &&
        context.guest_output_width == 1280 &&
        context.guest_output_height == 720) {
      auto scene = pinyon_shift::native_renderer::SnapshotSnr04LiveScene(
          context.frame_sequence);
      if (scene)
        return pinyon_shift::native_renderer::DrawNativeOutputTriangle(
            context, *scene);
    }
#endif
    if (REXCVAR_GET(pinyon_shift_native_scene_clear_probe) &&
        pinyon_shift::fh1_render_test::Enabled() &&
        context.guest_output_width == 1280 &&
        context.guest_output_height == 720 && context.clear_color) {
      auto scene = pinyon_shift::native_renderer::SnapshotSnr04LiveScene(
          context.frame_sequence);
      if (scene) {
        const float color[4]{float(scene->core_draws) / 4096.f, 0.375f,
                             (scene->source_frame & 1) ? 0.75f : 0.25f, 1.f};
        return context.clear_color(context, color);
      }
    }
    if (!REXCVAR_GET(pinyon_shift_native_output_clear_probe) ||
        !pinyon_shift::fh1_render_test::Enabled() ||
        context.guest_output_width != 1280 ||
        context.guest_output_height != 720 || !context.clear_color)
      return false;
    const float color[4]{0.125f, 0.375f,
                         (context.frame_sequence & 1) ? 0.75f : 0.25f, 1.f};
    return context.clear_color(context, color);
  }
  pinyon_shift::fh1_render_test::ObserveOutput(context);
  pinyon_shift::native_renderer::ObserveSnr04BatchOutputFrame(
      context.frame_sequence, context.device,
      captured_frame == context.frame_sequence ? capture_us : 0);
  return false;
}

}  // namespace

namespace pinyon_shift::native_renderer {

bool NativeRaceRequested() {
#if defined(_WIN32)
  return native_race_enabled.load(std::memory_order_acquire);
#else
  return false;
#endif
}

void InstallGuestOutputRenderer(rex::system::IGraphicsSystem* graphics_system) {
  if (graphics_system) {
#if defined(_WIN32)
    static std::once_flag native_race_callback_once;
    std::call_once(native_race_callback_once, [] {
      rex::cvar::RegisterChangeCallback(
          "pinyon_shift_native_race", [](std::string_view, std::string_view value) {
            native_race_enabled.store(value == "true", std::memory_order_release);
          });
    });
    native_race_enabled.store(REXCVAR_GET(pinyon_shift_native_race),
                              std::memory_order_release);
#endif
    graphics_system->SetNativeGuestOutputRenderer(&ObserveRenderTestOutput);
  }
}

void UninstallGuestOutputRenderer(
    rex::system::IGraphicsSystem* graphics_system) {
  if (graphics_system) {
    graphics_system->SetNativeGuestOutputRenderer(nullptr);
  }
  FinishSnr04BatchDiagnostic();
}

}  // namespace pinyon_shift::native_renderer
