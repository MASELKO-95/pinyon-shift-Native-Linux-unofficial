#include <rex/system/interfaces/graphics.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>

#include <chrono>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>

#include "fh1_render_test.h"
#include "native_renderer/graphics_hooks.h"
#include "native_renderer/guest_output_renderer.h"
#if defined(_WIN32)
#include <d3d12.h>
#include <wrl/client.h>

#include <rex/graphics/d3d12/deferred_command_list.h>

#include "native_renderer/native_output_track.h"
#include "native_renderer/native_output_ui.h"
#include "native_renderer/native_output_triangle.h"
#endif
#include "native_renderer/snr04_owned_scene_diagnostic.h"
#include "native_renderer/ordered_ui_capture.h"
#include "pinyon_shift_diagnostics.h"

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
REXCVAR_DEFINE_INT32(pinyon_shift_native_ui_replay_source_frame, 0,
                     "Pinyon Shift",
                     "Render-test pilot for ordered HUD replay")
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

using Microsoft::WRL::ComPtr;
struct ShadowFrame {
  uint64_t output_frame = 0, submission = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  ComPtr<ID3D12Resource> target, readback;
};
thread_local std::deque<ShadowFrame> shadow_pending;

void DrainShadow(uint64_t completed_submission) {
  const auto output = pinyon_shift::diagnostics::EnvironmentPath(
      "PINYON_SHIFT_FH1_RENDER_TEST_OUTPUT");
  while (!shadow_pending.empty() &&
         shadow_pending.front().submission <= completed_submission) {
    auto frame = std::move(shadow_pending.front());
    shadow_pending.pop_front();
    if (!output || !frame.readback) continue;
    void* mapped = nullptr;
    if (FAILED(frame.readback->Map(0, nullptr, &mapped))) continue;
    std::error_code error;
    std::filesystem::create_directories(*output, error);
    const auto path = *output /
        ("native-shadow-" + std::to_string(frame.output_frame) + ".ppm");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!error && file) {
      file << "P6\n1280 720\n255\n";
      const auto* bytes = static_cast<const uint8_t*>(mapped);
      for (uint32_t y = 0; y < 720; ++y) {
        const auto* row = reinterpret_cast<const uint32_t*>(
            bytes + y * frame.footprint.Footprint.RowPitch);
        for (uint32_t x = 0; x < 1280; ++x) {
          const uint32_t pixel = row[x];
          const char rgb[3]{char(((pixel & 1023) * 255 + 511) / 1023),
                            char((((pixel >> 10) & 1023) * 255 + 511) / 1023),
                            char((((pixel >> 20) & 1023) * 255 + 511) / 1023)};
          file.write(rgb, 3);
        }
      }
    }
    frame.readback->Unmap(0, nullptr);
  }
}

bool DrawShadow(const rex::system::NativeGuestOutputRenderContext& context,
                bool save_image) {
  auto* device = static_cast<ID3D12Device*>(context.device);
  auto* list = static_cast<rex::graphics::d3d12::DeferredCommandList*>(
      context.deferred_command_list);
  if (!device || !list || context.guest_output_width != 1280 ||
      context.guest_output_height != 720 || shadow_pending.size() >= 8)
    return false;
  auto scene = pinyon_shift::native_renderer::SnapshotSnr04LiveScene(
      context.frame_sequence);
  const uint64_t source_frame = context.frame_sequence - 1;
  if (!scene || !pinyon_shift::native_renderer::WithOrderedUiFrame(
                    source_frame, [](const auto&) { return true; }))
    return false;
  ShadowFrame frame;
  frame.output_frame = context.frame_sequence;
  frame.submission = context.submission;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = 1280;
  desc.Height = 720;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
  if (FAILED(device->CreateCommittedResource(
          &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
          nullptr, IID_PPV_ARGS(&frame.target)))) return false;
  auto shadow_context = context;
  shadow_context.guest_output = frame.target.Get();
  shadow_context.guest_output_state = D3D12_RESOURCE_STATE_COMMON;
  const bool scene_drawn = pinyon_shift::native_renderer::DrawNativeOutputTrack(
      shadow_context, *scene, true);
  const bool ui_drawn = scene_drawn &&
      pinyon_shift::native_renderer::DrawNativeOutputUi(
          shadow_context, source_frame);
  if (ui_drawn && save_image) {
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    device->GetCopyableFootprints(&desc, 0, 1, 0, &frame.footprint,
                                  nullptr, nullptr, nullptr);
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = frame.footprint.Footprint.RowPitch * 720ull;
    desc.Height = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (SUCCEEDED(device->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            IID_PPV_ARGS(&frame.readback)))) {
      D3D12_RESOURCE_BARRIER barrier{};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = frame.target.Get();
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
      list->D3DResourceBarrier(1, &barrier);
      D3D12_TEXTURE_COPY_LOCATION from{}, to{};
      from.pResource = frame.target.Get();
      from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
      to.pResource = frame.readback.Get();
      to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
      to.PlacedFootprint = frame.footprint;
      list->D3DCopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
      std::swap(barrier.Transition.StateBefore,
                barrier.Transition.StateAfter);
      list->D3DResourceBarrier(1, &barrier);
    }
  }
  shadow_pending.push_back(std::move(frame));
  REXGPU_WARN("FH1 UI shadow replay output_frame={} source_frame={} "
              "scene={} ui={}", context.frame_sequence, source_frame,
              scene_drawn, ui_drawn);
  return ui_drawn;
}
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
      if (scene && REXCVAR_GET(pinyon_shift_native_ui_scene_probe)) {
        const bool drawn = pinyon_shift::native_renderer::DrawNativeOutputTrack(
            context, *scene);
        rex::perf::TraceCriticalPath("native_scene_draw",
                                     int64_t(scene->source_frame), drawn);
        static const bool log_draw =
            rex::cvar::GetFlagByName("perf_critical_path_trace") == "true";
        if (log_draw)
          REXGPU_WARN("FH1 native scene draw source_frame={} drawn={}",
                      scene->source_frame, drawn);
        return drawn;
      }
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
    DrainShadow(context.completed_submission);
    static const uint64_t shadow_start = std::strtoull(
        rex::cvar::GetFlagByName(
            "pinyon_shift_native_ui_shadow_start_frame").c_str(),
        nullptr, 10);
    const uint64_t source_frame = context.frame_sequence - 1;
    if (shadow_start && pinyon_shift::fh1_render_test::Enabled() &&
        !native_race_enabled.load(std::memory_order_acquire) &&
        source_frame >= shadow_start &&
        source_frame - shadow_start < 24) {
      DrawShadow(context, source_frame - shadow_start >= 8 &&
                          source_frame - shadow_start < 16);
      return false;
    }
    // A missed early boundary must keep the complete guest frame. The late
    // compositor cannot safely recover UI over a partially replaced target.
    if (REXCVAR_GET(pinyon_shift_native_ui_clear_probe) ||
        REXCVAR_GET(pinyon_shift_native_ui_scene_probe))
      return false;
    const int32_t ui_replay_frame =
        REXCVAR_GET(pinyon_shift_native_ui_replay_source_frame);
    if (ui_replay_frame > 0 && pinyon_shift::fh1_render_test::Enabled() &&
        context.frame_sequence == uint64_t(ui_replay_frame) + 1 &&
        context.guest_output_width == 1280 &&
        context.guest_output_height == 720) {
      auto scene = pinyon_shift::native_renderer::SnapshotSnr04LiveScene(
          context.frame_sequence);
      const bool ui_ready = pinyon_shift::native_renderer::WithOrderedUiFrame(
          ui_replay_frame, [](const auto&) { return true; });
      REXGPU_WARN("FH1 UI pilot admission scene={} ui={}", bool(scene), ui_ready);
      if (!scene || !ui_ready)
        return false;
      const bool scene_drawn = pinyon_shift::native_renderer::DrawNativeOutputTrack(
          context, *scene, true);
      const bool ui_drawn = scene_drawn &&
          pinyon_shift::native_renderer::DrawNativeOutputUi(
              context, ui_replay_frame);
      REXGPU_WARN("FH1 UI pilot output scene={} ui={}", scene_drawn, ui_drawn);
      return ui_drawn;
    }
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
  pinyon_shift::native_renderer::FlushOrderedUiFrame(context.frame_sequence);
#if defined(_WIN32)
  DrainShadow(context.completed_submission);
#endif
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
