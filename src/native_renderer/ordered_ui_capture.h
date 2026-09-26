#pragma once

#include <cstdint>

namespace rex::system {
struct GraphicsPreparedDrawObservation;
struct GraphicsFinalDrawStateObservation;
struct GraphicsCopyObservation;
struct GraphicsFh1ClearObservation;
}

namespace pinyon_shift::native_renderer {

void CaptureOrderedUiDraw(
    const rex::system::GraphicsPreparedDrawObservation& observation);
void CaptureOrderedFrameDraw(
    const rex::system::GraphicsPreparedDrawObservation& observation);
void CaptureOrderedFrameCopy(
    const rex::system::GraphicsCopyObservation& observation);
void CaptureOrderedFrameClear(
    const rex::system::GraphicsFh1ClearObservation& observation);
void CaptureOrderedUiFinalState(
    const rex::system::GraphicsFinalDrawStateObservation& observation);
void FlushOrderedUiFrame(uint64_t output_frame);

}  // namespace pinyon_shift::native_renderer
