#pragma once

#include <cstdint>

namespace rex::system {
struct GraphicsPreparedDrawObservation;
struct GraphicsFinalDrawStateObservation;
}

namespace pinyon_shift::native_renderer {

void CaptureOrderedUiDraw(
    const rex::system::GraphicsPreparedDrawObservation& observation);
void CaptureOrderedUiFinalState(
    const rex::system::GraphicsFinalDrawStateObservation& observation);
void FlushOrderedUiFrame(uint64_t output_frame);

}  // namespace pinyon_shift::native_renderer
