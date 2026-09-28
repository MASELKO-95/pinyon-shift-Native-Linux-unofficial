#pragma once

namespace rex::system {
class IGraphicsSystem;
}
namespace rex::memory {
class Memory;
}

namespace pinyon_shift::native_renderer {

// Registers the FH1 GPU corpus observers (pinyon_shift_fh1_gpu_corpus).
void InstallGraphicsCensus(rex::system::IGraphicsSystem* graphics_system,
                           rex::memory::Memory* memory);
void UninstallGraphicsCensus(rex::system::IGraphicsSystem* graphics_system);

}  // namespace pinyon_shift::native_renderer
