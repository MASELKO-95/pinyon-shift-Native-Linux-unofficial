#pragma once

#include <cstdint>

#include <rex/system/interfaces/graphics.h>

namespace pinyon_shift::native_renderer {

bool ResetFh1GpuCorpus();
void RecordFh1GpuExecution(
    const rex::system::GraphicsFh1ExecutionKey& key, uint64_t frame,
    uint64_t vertex_shader = 0, uint64_t pixel_shader = 0);
void FlushFh1GpuCorpus(bool final = true);

}  // namespace pinyon_shift::native_renderer
