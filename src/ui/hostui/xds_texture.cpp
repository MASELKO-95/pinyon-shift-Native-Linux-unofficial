#include "ui/hostui/xds_texture.h"

#include <algorithm>
#include <array>
#include <bit>

namespace pinyon_shift::hostui {
namespace {

constexpr size_t kHeaderSize = 0x34;
constexpr size_t kFetchOffset = 0x1C;

struct Format {
  uint32_t number;
  uint32_t block;        // texels per block edge
  uint32_t block_bytes;
};

// The formats the UI archives use (Xenos TextureFormat numbers).
constexpr std::array<Format, 9> kFormats = {{
    {6, 1, 4},    // k_8_8_8_8
    {18, 4, 8},   // k_DXT1
    {19, 4, 16},  // k_DXT2_3
    {20, 4, 16},  // k_DXT4_5
    {50, 1, 4},   // k_8_8_8_8_AS_16_16_16_16
    {51, 4, 8},   // k_DXT1_AS_16_16_16_16
    {52, 4, 16},  // k_DXT2_3_AS_16_16_16_16
    {53, 4, 16},  // k_DXT4_5_AS_16_16_16_16
    {58, 4, 8},   // k_DXT3A
}};
constexpr Format kDxt5a = {59, 4, 8};

// Byte permutations for none, 8-in-16, 8-in-32 and 16-in-32 endianness.
constexpr std::array<uint32_t, 4> kEndianMasks = {0, 1, 3, 2};

using Texel = std::array<uint8_t, 4>;

uint32_t ReadBe32(std::span<const uint8_t> data, size_t at) {
  return uint32_t(data[at]) << 24 | uint32_t(data[at + 1]) << 16 | uint32_t(data[at + 2]) << 8 |
         uint32_t(data[at + 3]);
}

uint32_t Log2Ceil(uint32_t value) { return value <= 1 ? 0 : uint32_t(std::bit_width(value - 1)); }

void Bc1Palette(const uint8_t* block, bool allow_transparent, std::array<Texel, 4>& palette) {
  const uint32_t color0 = block[0] | block[1] << 8;
  const uint32_t color1 = block[2] | block[3] << 8;
  const auto expand = [](uint32_t color) {
    const uint32_t r = color >> 11, g = (color >> 5) & 0x3F, b = color & 0x1F;
    return Texel{uint8_t(r << 3 | r >> 2), uint8_t(g << 2 | g >> 4), uint8_t(b << 3 | b >> 2), 255};
  };
  palette[0] = expand(color0);
  palette[1] = expand(color1);
  for (int c = 0; c < 3; ++c) {
    const uint32_t first = palette[0][c], second = palette[1][c];
    if (color0 > color1 || !allow_transparent) {
      palette[2][c] = uint8_t((2 * first + second + 1) / 3);
      palette[3][c] = uint8_t((first + 2 * second + 1) / 3);
    } else {
      palette[2][c] = uint8_t((first + second + 1) / 2);
      palette[3][c] = 0;
    }
  }
  palette[2][3] = 255;
  palette[3][3] = (color0 > color1 || !allow_transparent) ? 255 : 0;
}

void InterpolatedAlpha(const uint8_t* block, std::array<uint8_t, 16>& out) {
  const uint32_t a0 = block[0], a1 = block[1];
  std::array<uint8_t, 8> palette = {uint8_t(a0), uint8_t(a1)};
  if (a0 > a1) {
    for (uint32_t i = 1; i < 7; ++i) {
      palette[i + 1] = uint8_t(((7 - i) * a0 + i * a1 + 3) / 7);
    }
  } else {
    for (uint32_t i = 1; i < 5; ++i) {
      palette[i + 1] = uint8_t(((5 - i) * a0 + i * a1 + 2) / 5);
    }
    palette[6] = 0;
    palette[7] = 255;
  }
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) {
    bits |= uint64_t(block[2 + i]) << (8 * i);
  }
  for (int texel = 0; texel < 16; ++texel) {
    out[texel] = palette[(bits >> (3 * texel)) & 7];
  }
}

void ExplicitAlpha(const uint8_t* block, std::array<uint8_t, 16>& out) {
  for (int texel = 0; texel < 16; ++texel) {
    out[texel] = uint8_t(((block[texel / 2] >> (4 * (texel & 1))) & 0xF) * 17);
  }
}

// Decodes one block (already in little-endian order) to XYZW texels.
void DecodeBlock(uint32_t format, const uint8_t* block, std::array<Texel, 16>& texels) {
  if (format == 6 || format == 50) {
    texels[0] = {block[0], block[1], block[2], block[3]};
    return;
  }
  std::array<uint8_t, 16> alpha;
  if (format == 58 || format == 59) {
    // One-channel formats read the same value in every component, as the
    // SDK's RRRR host swizzle for them does.
    format == 58 ? ExplicitAlpha(block, alpha) : InterpolatedAlpha(block, alpha);
    for (int texel = 0; texel < 16; ++texel) {
      texels[texel] = {alpha[texel], alpha[texel], alpha[texel], alpha[texel]};
    }
    return;
  }
  const bool has_alpha_block = format == 19 || format == 20 || format == 52 || format == 53;
  const uint8_t* colors = block + (has_alpha_block ? 8 : 0);
  std::array<Texel, 4> palette;
  Bc1Palette(colors, !has_alpha_block, palette);
  const uint32_t indices = colors[4] | colors[5] << 8 | colors[6] << 16 | uint32_t(colors[7]) << 24;
  if (has_alpha_block) {
    (format == 19 || format == 52) ? ExplicitAlpha(block, alpha) : InterpolatedAlpha(block, alpha);
  }
  for (int texel = 0; texel < 16; ++texel) {
    texels[texel] = palette[(indices >> (2 * texel)) & 3];
    if (has_alpha_block) {
      texels[texel][3] = alpha[texel];
    }
  }
}

}  // namespace

int32_t TiledOffset2D(int32_t x, int32_t y, uint32_t pitch, uint32_t bytes_per_block_log2) {
  pitch = (pitch + 31) & ~31u;
  const int32_t macro = ((x >> 5) + (y >> 5) * int32_t(pitch >> 5)) << (bytes_per_block_log2 + 7);
  const int32_t micro = ((x & 7) + ((y & 0xE) << 2)) << bytes_per_block_log2;
  const int32_t offset = macro + ((micro & ~0xF) << 1) + (micro & 0xF) + ((y & 1) << 4);
  return ((offset & ~0x1FF) << 3) + ((y & 16) << 7) + ((offset & 0x1C0) << 2) +
         (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 0x3F);
}

std::optional<RgbaImage> DecodeXds(std::span<const uint8_t> data) {
  if (data.size() < kHeaderSize || ReadBe32(data, 0) != 3) {
    return std::nullopt;
  }
  std::array<uint32_t, 6> fetch;
  for (size_t i = 0; i < fetch.size(); ++i) {
    fetch[i] = ReadBe32(data, kFetchOffset + 4 * i);
  }
  if ((fetch[0] & 3) != 2 || ((fetch[5] >> 9) & 3) != 1) {
    return std::nullopt;  // not a 2D texture fetch constant
  }
  const uint32_t format_number = fetch[1] & 0x3F;
  const Format* format = format_number == kDxt5a.number ? &kDxt5a : nullptr;
  for (const Format& candidate : kFormats) {
    if (candidate.number == format_number) {
      format = &candidate;
    }
  }
  if (!format) {
    return std::nullopt;
  }
  const uint32_t width = (fetch[2] & 0x1FFF) + 1;
  const uint32_t height = ((fetch[2] >> 13) & 0x1FFF) + 1;
  const uint32_t pitch_texels = ((fetch[0] >> 22) & 0x1FF) * 32;
  const bool tiled = fetch[0] >> 31;
  const uint32_t mask = kEndianMasks[(fetch[1] >> 6) & 3];
  const uint32_t swizzle = (fetch[3] >> 1) & 0xFFF;
  const bool packed_mips = (fetch[5] >> 11) & 1;

  uint32_t offset_x = 0, offset_y = 0;
  if (packed_mips && std::min(Log2Ceil(width), Log2Ceil(height)) <= 4) {
    // A base level no larger than 16 texels sits in the packed mip tail.
    (Log2Ceil(width) > Log2Ceil(height) ? offset_y : offset_x) = 16 / format->block;
  }
  const uint32_t columns = (width + format->block - 1) / format->block;
  const uint32_t rows = (height + format->block - 1) / format->block;
  const uint32_t pitch = std::max(pitch_texels / format->block, 1u);
  const uint32_t log2 = uint32_t(std::bit_width(format->block_bytes) - 1);
  const std::span<const uint8_t> payload = data.subspan(kHeaderSize);

  RgbaImage image;
  image.width = width;
  image.height = height;
  image.rgba.assign(size_t(width) * height * 4, 0);
  std::array<uint8_t, 16> block;
  std::array<Texel, 16> texels;
  for (uint32_t y = 0; y < rows; ++y) {
    for (uint32_t x = 0; x < columns; ++x) {
      const size_t offset =
          tiled ? size_t(TiledOffset2D(int32_t(x + offset_x), int32_t(y + offset_y), pitch, log2))
                : (size_t(y + offset_y) * pitch + x + offset_x) * format->block_bytes;
      if (offset + format->block_bytes > payload.size()) {
        return std::nullopt;
      }
      for (uint32_t i = 0; i < format->block_bytes; ++i) {
        block[i] = payload[offset + (i ^ mask)];
      }
      DecodeBlock(format->number, block.data(), texels);
      const uint32_t texel_count = format->block * format->block;
      for (uint32_t i = 0; i < texel_count; ++i) {
        const uint32_t texel_x = x * format->block + i % format->block;
        const uint32_t texel_y = y * format->block + i / format->block;
        if (texel_x >= width || texel_y >= height) {
          continue;
        }
        uint8_t* out = &image.rgba[(size_t(texel_y) * width + texel_x) * 4];
        for (int channel = 0; channel < 4; ++channel) {
          const uint32_t selector = (swizzle >> (3 * channel)) & 7;
          out[channel] = selector < 4 ? texels[i][selector] : (selector == 5 ? 255 : 0);
        }
      }
    }
  }
  return image;
}

}  // namespace pinyon_shift::hostui
