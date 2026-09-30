#include "save/car_cards.h"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <fmt/chrono.h>
#include <fmt/format.h>

namespace pinyon_shift::save {
namespace fs = std::filesystem;
namespace {

uint32_t LoadBe32(const uint8_t* bytes) {
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) |
         uint32_t(bytes[3]);
}

bool IsCarCardName(const fs::path& path) {
  const std::string name = path.filename().string();
  return path.parent_path().filename() == "Thumbnails" && name.rfind("Thumbnail_", 0) == 0 &&
         path.extension() == ".xdc";
}

}  // namespace

bool IsStripedCarCard(std::span<const uint8_t> header) {
  if (header.size() < 16 || std::memcmp(header.data(), "cxds", 4) != 0) {
    return false;
  }
  const uint32_t uncompressed = LoadBe32(header.data() + 8);
  const uint32_t compressed = LoadBe32(header.data() + 12);
  // Rendered cards measure 3-4%, striped ones 31-63%.
  return uncompressed != 0 && uint64_t(compressed) * 100 > uint64_t(uncompressed) * 15;
}

size_t QuarantineStripedCarCards(const fs::path& user_root, const fs::path& backup_root) {
  std::error_code error;
  if (!fs::exists(user_root, error)) {
    return 0;
  }
  std::vector<fs::path> striped;
  for (auto it = fs::recursive_directory_iterator(user_root, error);
       !error && it != fs::recursive_directory_iterator(); it.increment(error)) {
    if (!it->is_regular_file(error) || !IsCarCardName(it->path())) {
      continue;
    }
    std::array<uint8_t, 16> header{};
    std::ifstream file(it->path(), std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(header.data()), header.size())) {
      continue;
    }
    if (IsStripedCarCard(header)) {
      striped.push_back(it->path());
    }
  }
  if (striped.empty()) {
    return 0;
  }
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  const fs::path slot = backup_root / fmt::format("{:%Y%m%dT%H%M%S}Z", now);
  size_t moved = 0;
  for (const fs::path& card : striped) {
    const fs::path target = slot / fs::relative(card, user_root, error);
    if (error) {
      continue;
    }
    fs::create_directories(target.parent_path(), error);
    // Copy, then remove, so an interrupted move never loses the card.
    if (!fs::copy_file(card, target, fs::copy_options::overwrite_existing, error) || error) {
      continue;
    }
    if (fs::remove(card, error) && !error) {
      ++moved;
    }
  }
  return moved;
}

}  // namespace pinyon_shift::save
