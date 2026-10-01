#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace pinyon_shift::ui {

// Encodes 8-bit RGB (the first three bytes of each `bytes_per_pixel` pixel,
// rows `stride` bytes apart) as a PNG: adaptive None/Sub/Up row filters and a
// zlib stream of fixed-Huffman deflate with greedy LZ77 over a 32 KiB window.
// Self-contained so photo export needs no image library.
std::vector<uint8_t> EncodePng(const uint8_t* pixels, uint32_t width, uint32_t height,
                               size_t stride, uint32_t bytes_per_pixel);

}  // namespace pinyon_shift::ui
