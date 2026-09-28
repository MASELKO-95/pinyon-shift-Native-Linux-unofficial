#include "native_renderer/guest_output_renderer.h"

#include <rex/system/interfaces/graphics.h>

#include "fh1_render_test.h"

namespace {

// Render-test captures and waits run on each presented output frame.
bool ObserveRenderTestOutput(const rex::system::NativeGuestOutputRenderContext& context) {
  if (context.phase == rex::system::NativeGuestOutputPhase::kPresented) {
    pinyon_shift::fh1_render_test::ObserveOutput(context);
  }
  return false;
}

}  // namespace

namespace pinyon_shift::native_renderer {

void InstallGuestOutputRenderer(rex::system::IGraphicsSystem* graphics_system) {
  if (graphics_system) {
    graphics_system->SetNativeGuestOutputRenderer(&ObserveRenderTestOutput);
  }
}

void UninstallGuestOutputRenderer(rex::system::IGraphicsSystem* graphics_system) {
  if (graphics_system) {
    graphics_system->SetNativeGuestOutputRenderer(nullptr);
  }
}

}  // namespace pinyon_shift::native_renderer
