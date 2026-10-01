"""Check the production key normalization against real guest texture layouts."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--source', required=True, type=Path,
                        help='cache.cpp containing the isolated packed-tail candidate')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    sdk = repo / 'thirdparty/shiftglue-sdk'
    source = args.source.read_text()
    block = source.split('// BEGIN FH1 UNUSED PACKED TAIL\n', 1)[1].split(
        '// END FH1 UNUSED PACKED TAIL', 1)[0]
    harness = r'''
#include <rex/graphics/pipeline/texture/util.h>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <tuple>
// Standalone equivalent of the runtime bit-count primitive.
namespace rex { uint8_t lzcnt(uint32_t v) { return std::countl_zero(v); } }
using namespace rex::graphics;
void check(bool value) { if (!value) throw std::runtime_error("check failed"); }
struct Key {
  xenos::DataDimension dimension;
  uint32_t depth_or_array_size_minus_1, mip_max_level, packed_mips, width, height;
  uint32_t GetWidth() const { return width; }
  uint32_t GetHeight() const { return height; }
};
void normalize(Key& key_out) { PRODUCTION_BLOCK }
auto fields(const texture_util::TextureGuestLayout::Level& v) {
  return std::tie(v.row_pitch_bytes, v.z_slice_stride_block_rows,
      v.array_slice_stride_bytes, v.x_extent_blocks, v.y_extent_blocks,
      v.z_extent, v.array_slice_data_extent_bytes, v.level_data_extent_bytes);
}
int main() {
  unsigned layouts = 0;
  for (uint32_t width : {1u, 8u, 16u, 17u, 31u, 32u, 96u, 320u, 1280u, 8192u})
  for (uint32_t height : {1u, 16u, 17u, 32u, 64u, 180u, 720u, 8192u})
  for (auto dimension : {xenos::DataDimension::k1D, xenos::DataDimension::k2DOrStacked,
                        xenos::DataDimension::k3D, xenos::DataDimension::kCube})
  for (uint32_t array : {0u, 1u})
  for (uint32_t mips : {0u, 1u, 5u})
  for (uint32_t packed : {0u, 1u}) {
    Key key{dimension, array, mips, packed, width, height};
    normalize(key);
    bool admitted = dimension == xenos::DataDimension::k2DOrStacked && !array &&
                    !mips && width > 16 && height > 16;
    check(key.packed_mips == (admitted ? 0u : packed));
    if (!admitted || !packed) continue;
    for (bool tiled : {false, true})
    for (auto format : {xenos::TextureFormat::k_8, xenos::TextureFormat::k_8_8_8_8,
          xenos::TextureFormat::k_2_10_10_10, xenos::TextureFormat::k_16_16_16_16,
          xenos::TextureFormat::k_32_32_32_32, xenos::TextureFormat::k_24_8_FLOAT,
          xenos::TextureFormat::k_DXT1, xenos::TextureFormat::k_DXT4_5})
    for (uint32_t pitch : {(width + 31) / 32, (width + 31) / 32 + 8}) {
      auto old = texture_util::GetGuestTextureLayout(dimension, pitch, width, height,
          1, tiled, format, true, true, 0);
      auto normalized = texture_util::GetGuestTextureLayout(dimension, pitch, width, height,
          1, tiled, format, false, true, 0);
      check(old.packed_level > 0 && normalized.packed_level > 0);
      check(fields(old.base) == fields(normalized.base));
      check(!old.mips_total_extent_bytes && !normalized.mips_total_extent_bytes);
      check(old.array_size == normalized.array_size && old.max_level == normalized.max_level);
      uint32_t x, y, z;
      check(!texture_util::GetPackedMipOffset(width, height, 1, format, 0, x, y, z));
      check(x == 0 && y == 0 && z == 0);
      ++layouts;
    }
  }
  check(layouts > 1000);
  std::cout << layouts << " production base layouts match; admission boundaries pass\n";
}
'''.replace('PRODUCTION_BLOCK', block)
    with tempfile.TemporaryDirectory(prefix='fh1-packed-tail-') as temp:
        cpp, exe = Path(temp) / 'check.cpp', Path(temp) / 'check.exe'
        cpp.write_text(harness)
        subprocess.run([args.compiler, '-std=c++23', '-DNDEBUG', '-I' + str(sdk / 'include'),
                        str(cpp), str(sdk / 'src/graphics/pipeline/texture/util.cpp'),
                        str(sdk / 'src/graphics/pipeline/texture/info_formats.cpp'),
                        '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
