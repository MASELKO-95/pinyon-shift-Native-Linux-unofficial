#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace pinyon_shift::hostui {

struct RgbaImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> rgba;  // tightly packed, straight alpha
};

// Byte offset of block (x, y) in a Xenos 2D tiled surface whose pitch is in
// blocks (the SDK's texture_util::GetTiledOffset2D).
int32_t TiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2);

// Decodes the base level of an FH1 `.xds` UI texture: a 52-byte D3D texture
// header with the Xenos fetch constant, then the tiled data. Supports the
// formats the UI archives use (RGBA8, BC1 to BC4, DXT3A); see
// docs/UI_ASSETS.md.
std::optional<RgbaImage> DecodeXds(std::span<const uint8_t> data);

}  // namespace pinyon_shift::hostui
