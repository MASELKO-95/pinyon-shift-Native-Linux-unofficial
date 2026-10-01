#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace pinyon_shift::save {

// A car card (`ForzaProfile/Thumbnails/Thumbnail_N.xdc`) is a compressed
// 768x288 texture: "cxds", a version, the uncompressed size and the
// compressed size, all big-endian. A card the game rendered compresses to
// about 4% of its size; builds before SDK 0bf0658 saved stale memory instead
// (the striped cards), which compresses to 30% or more. True for such a card,
// from the file's first 16 bytes.
bool IsStripedCarCard(std::span<const uint8_t> header);

// Moves every striped car card under `user_root` into
// `backup_root/<UTC stamp>/<same relative path>`, so the title shows an empty
// card instead of noise and the originals stay restorable. Returns how many
// cards were moved.
size_t QuarantineStripedCarCards(const std::filesystem::path& user_root,
                                 const std::filesystem::path& backup_root);

}  // namespace pinyon_shift::save
