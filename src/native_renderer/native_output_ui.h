#pragma once

#include <cstdint>

namespace rex::system {
struct NativeGuestOutputRenderContext;
}

namespace pinyon_shift::native_renderer {

// Render-test pilot: replay the original untextured HUD shader pair.
bool DrawNativeOutputUi(const rex::system::NativeGuestOutputRenderContext& context,
                        uint64_t source_frame);

}  // namespace pinyon_shift::native_renderer
