// CPU BC3 asset import qualification. Uses the existing format/layout helpers;
// no command processor, TextureCache, GPU device or shader loader is linked.
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <rex/graphics/pipeline/texture/bc3_import.h>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace tex = rex::graphics::texture_util;
namespace xe = rex::graphics::xenos;
using Bytes = std::vector<uint8_t>;

static void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

static Bytes read(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  require(bool(file), "Missing input");
  return {std::istreambuf_iterator<char>(file), {}};
}

static void selftest() {
  xe::xe_gpu_texture_fetch_t fetch{};
  fetch.type = xe::FetchConstantType::kTexture;
  fetch.dimension = xe::DataDimension::k2DOrStacked;
  fetch.format = xe::TextureFormat::k_DXT4_5;
  fetch.base_address = 1;
  fetch.pitch = 1;
  fetch.size_2d.width = fetch.size_2d.height = 3;
  Bytes data(4096);
  for (uint32_t i = 0; i < 16; ++i)
    data[i] = uint8_t(i);
  const Bytes expected[] = {
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
      {1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14},
      {3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12},
      {2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13}};
  for (uint32_t endian = 0; endian < 4; ++endian) {
    fetch.endianness = xe::Endian(endian);
    require(tex::ImportBc3(fetch, data, {}) == expected[endian],
            "Endian conversion failed");
  }
  fetch.format = xe::TextureFormat::k_DXT1;
  bool rejected = false;
  try {
    tex::ImportBc3(fetch, data, {});
  } catch (const std::runtime_error &) {
    rejected = true;
  }
  require(rejected, "Unsupported format accepted");
}

int main(int argc, char **argv) try {
  selftest();
  if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
    std::cout << "BC3 import self-test passed\n";
    return 0;
  }
  require(argc == 3,
          "Usage: fh1_texture_import snapshot-directory new-output-directory");
  const fs::path input = argv[1], output = argv[2];
  require(!fs::exists(output) && fs::create_directories(output),
          "Output must be new");
  size_t count = 0, total = 0;
  for (const auto &entry : fs::directory_iterator(input)) {
    if (!entry.is_directory())
      continue;
    const auto raw_fetch = read(entry.path() / "fetch.bin");
    require(raw_fetch.size() == sizeof(xe::xe_gpu_texture_fetch_t),
            "Invalid fetch size");
    xe::xe_gpu_texture_fetch_t fetch;
    std::memcpy(&fetch, raw_fetch.data(), sizeof(fetch));
    const auto base = read(entry.path() / "base.bin"),
               mips = read(entry.path() / "mips.bin");
    const auto decoded = tex::ImportBc3(fetch, base, mips);
    require(decoded == read(entry.path() / "expected.bc3"),
            "CPU/GPU BC3 mismatch");
    bool rejected = false;
    try {
      tex::ImportBc3(fetch, {}, mips);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    require(rejected, "Truncated input accepted");
    uint32_t width, height, max_level;
    tex::GetSubresourcesFromFetchConstant(
        fetch, &width, &height, nullptr, nullptr, nullptr, nullptr, &max_level);
    ++width;
    ++height;
    if (max_level) {
      rejected = false;
      try {
        tex::ImportBc3(fetch, base, {});
      } catch (const std::runtime_error &) {
        rejected = true;
      }
      require(rejected, "Truncated mips accepted");
    }
    uint32_t x = 0, y = 0, z = 0;
    if (fetch.packed_mips)
      tex::GetPackedMipOffset(width, height, 1, xe::TextureFormat::k_DXT4_5, 0,
                              x, y, z);
    auto layout = tex::GetGuestTextureLayout(
        fetch.dimension, fetch.pitch, width, height, 1, fetch.tiled,
        xe::TextureFormat::k_DXT4_5, fetch.packed_mips, true, max_level);
    auto poisoned = base;
    const size_t first =
        fetch.tiled
            ? tex::GetTiledOffset2D(x, y, layout.base.row_pitch_bytes / 16, 4)
            : y * layout.base.row_pitch_bytes + x * 16;
    poisoned.at(first) ^= 1;
    require(tex::ImportBc3(fetch, poisoned, mips) != decoded,
            "Source mutation control failed");
    std::array<uint32_t, 32> dds{};
    dds[0] = 0x20534444;
    dds[1] = 124;
    dds[2] = 0xA1007;
    dds[3] = height;
    dds[4] = width;
    dds[5] = ((width + 3) / 4) * ((height + 3) / 4) * 16;
    dds[7] = max_level + 1;
    dds[19] = 32;
    dds[20] = 4;
    dds[21] = 0x35545844;
    dds[27] = max_level ? 0x401008 : 0x1000;
    std::ofstream file(output / (entry.path().filename().string() + ".dds"),
                       std::ios::binary);
    file.write(reinterpret_cast<const char *>(dds.data()), sizeof(dds));
    file.write(reinterpret_cast<const char *>(decoded.data()), decoded.size());
    file.close();
    require(bool(file), "DDS write failed");
    ++count;
    total += decoded.size();
  }
  require(count != 0, "No texture snapshots");
  std::cout << count << " textures, " << total
            << " BC3 bytes match; DDS assets written\n";
  return 0;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
