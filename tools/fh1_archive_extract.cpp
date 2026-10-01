#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <rex/kernel/init.h>
#include <rex/runtime.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>

#include "ui/hostui/fh1_archive.h"

using pinyon_shift::hostui::DecompressXmem;
using pinyon_shift::hostui::ParseXmemPayload;

int main(int argc, char** argv) {
  if (argc == 4 && std::string(argv[1]) == "--xex-image") {
    try {
      using rex::X_STATUS;
      const auto input = std::filesystem::canonical(argv[2]);
      rex::Runtime runtime(input.parent_path().string());
      if (runtime.Setup(rex::RuntimeConfig{
              .kernel_init = rex::kernel::InitializeKernel, .tool_mode = true}) !=
              X_STATUS_SUCCESS ||
          runtime.LoadXexImage("game:\\" + input.filename().string()) !=
              X_STATUS_SUCCESS) {
        std::cerr << "failed to load XEX image\n";
        return 1;
      }
      const auto* module = runtime.kernel_state()->GetExecutableModule()->xex_module();
      std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);
      output.write(reinterpret_cast<const char*>(
                       runtime.memory()->TranslateVirtual(module->base_address())),
                   module->image_size());
      output.close();
      return output ? 0 : 1;
    } catch (const std::exception& error) {
      std::cerr << error.what() << '\n';
      return 1;
    }
  }
  if (argc == 2 && std::string(argv[1]) == "--self-test") {
    const std::vector<uint8_t> framed = {
        0x00, 0x03, 1, 2, 3, 0xFF, 0x00, 0x02, 0x00, 0x02, 4, 5};
    std::vector<uint8_t> payload;
    return ParseXmemPayload(framed, 0x8002, payload) &&
                   payload == std::vector<uint8_t>({1, 2, 3, 4, 5})
               ? 0
               : 1;
  }
  if (argc != 6) {
    std::cerr << "usage: fh1_archive_extract <archive> <offset> <compressed-size> "
                 "<uncompressed-size> <output>\n";
    return 2;
  }

  uint64_t offset;
  size_t compressed_size;
  size_t uncompressed_size;
  try {
    offset = std::stoull(argv[2]);
    compressed_size = std::stoull(argv[3]);
    uncompressed_size = std::stoull(argv[4]);
  } catch (const std::exception&) {
    std::cerr << "invalid archive entry range\n";
    return 2;
  }
  constexpr size_t kMaximumShaderEntrySize = 16 * 1024 * 1024;
  if (!compressed_size || !uncompressed_size ||
      compressed_size > kMaximumShaderEntrySize ||
      uncompressed_size > kMaximumShaderEntrySize) {
    std::cerr << "archive shader entry is outside the supported size range\n";
    return 2;
  }
  std::ifstream input(argv[1], std::ios::binary);
  std::vector<uint8_t> compressed(compressed_size);
  input.seekg(static_cast<std::streamoff>(offset));
  if (!input.read(reinterpret_cast<char*>(compressed.data()), compressed.size())) {
    std::cerr << "unable to read compressed archive entry\n";
    return 1;
  }

  const auto decompressed = DecompressXmem(compressed, uncompressed_size);
  if (!decompressed) {
    std::cerr << "XMem LZX decompression failed\n";
    return 1;
  }
  const std::vector<uint8_t>& output = *decompressed;

  const std::filesystem::path output_path(argv[5]);
  std::error_code error;
  if (!output_path.parent_path().empty()) {
    std::filesystem::create_directories(output_path.parent_path(), error);
  }
  if (error) {
    std::cerr << "unable to create output directory\n";
    return 1;
  }
  std::ofstream output_file(output_path, std::ios::binary | std::ios::trunc);
  output_file.write(reinterpret_cast<const char*>(output.data()), output.size());
  return output_file ? 0 : 1;
}
