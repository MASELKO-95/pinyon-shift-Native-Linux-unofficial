#pragma once

#include <cstdint>

namespace rex::system {
struct NativeGuestOutputRenderContext;
}

namespace pinyon_shift::native_renderer {

// Render-test pilot: replay the original captured HUD shader pairs.
bool DrawNativeOutputUi(const rex::system::NativeGuestOutputRenderContext& context,
                        uint64_t source_frame);

}  // namespace pinyon_shift::native_renderer
