#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

#include "save/car_cards.h"

using namespace pinyon_shift::save;
namespace fs = std::filesystem;

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,   \
                   #condition);                                                 \
      ++failures;                                                               \
    }                                                                           \
  } while (false)

void Be32(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(uint8_t(value >> 24));
  out.push_back(uint8_t(value >> 16));
  out.push_back(uint8_t(value >> 8));
  out.push_back(uint8_t(value));
}

// A card header as the title writes it, padded to `compressed` + 16 bytes.
std::vector<uint8_t> Card(uint32_t compressed) {
  std::vector<uint8_t> card = {'c', 'x', 'd', 's'};
  Be32(card, 2);
  Be32(card, 884788);
  Be32(card, compressed);
  card.resize(size_t(compressed) + 16, 0x5A);
  return card;
}

void Write(const fs::path& path, const std::vector<uint8_t>& bytes) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary)
      .write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

}  // namespace

int main() {
  // Sizes from real cards: rendered 28,605 to 34,123 bytes, striped 275,528
  // to 559,241 of 884,788.
  CHECK(!IsStripedCarCard(Card(28605)));
  CHECK(!IsStripedCarCard(Card(34123)));
  CHECK(IsStripedCarCard(Card(275528)));
  CHECK(IsStripedCarCard(Card(559241)));
  std::vector<uint8_t> not_a_card = Card(559241);
  not_a_card[0] = 'x';
  CHECK(!IsStripedCarCard(not_a_card));
  CHECK(!IsStripedCarCard(std::vector<uint8_t>(8, 0)));

  const fs::path root = fs::temp_directory_path() / "pinyon_car_cards_test";
  fs::remove_all(root);
  const fs::path profile = root / "user" / "B13EBABEBABEBABE" / "4D5309C9" / "00000001" / "ForzaProfile";
  Write(profile / "Thumbnails" / "Thumbnail_3.xdc", Card(559241));
  Write(profile / "Thumbnails" / "Thumbnail_9.xdc", Card(28605));
  Write(profile / "ForzaProfile", Card(559241));  // not a card: left alone

  CHECK(QuarantineStripedCarCards(root / "user", root / "backups") == 1);
  CHECK(!fs::exists(profile / "Thumbnails" / "Thumbnail_3.xdc"));
  CHECK(fs::exists(profile / "Thumbnails" / "Thumbnail_9.xdc"));
  CHECK(fs::exists(profile / "ForzaProfile"));
  size_t backed_up = 0;
  for (const auto& entry : fs::recursive_directory_iterator(root / "backups")) {
    if (entry.path().filename() == "Thumbnail_3.xdc" && fs::file_size(entry.path()) == 559257) {
      ++backed_up;
    }
  }
  CHECK(backed_up == 1);
  // Nothing left to move.
  CHECK(QuarantineStripedCarCards(root / "user", root / "backups") == 0);
  fs::remove_all(root);

  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("car card tests passed\n");
  return 0;
}
