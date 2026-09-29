#include "ui/png_writer.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace pinyon_shift::ui {
namespace {

// Deflate emits bits least significant first; Huffman codes go in most
// significant bit first.
class BitWriter {
 public:
  explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}
  void Bits(uint32_t value, uint32_t count) {
    buffer_ |= uint64_t(value) << used_;
    used_ += count;
    while (used_ >= 8) {
      out_.push_back(uint8_t(buffer_));
      buffer_ >>= 8;
      used_ -= 8;
    }
  }
  void Code(uint32_t code, uint32_t length) {
    uint32_t reversed = 0;
    for (uint32_t i = 0; i < length; ++i) {
      reversed |= ((code >> i) & 1) << (length - 1 - i);
    }
    Bits(reversed, length);
  }
  void Flush() {
    if (used_) {
      out_.push_back(uint8_t(buffer_));
    }
    buffer_ = 0;
    used_ = 0;
  }

 private:
  std::vector<uint8_t>& out_;
  uint64_t buffer_ = 0;
  uint32_t used_ = 0;
};

// The fixed literal/length code (RFC 1951 3.2.6).
void Literal(BitWriter& bits, uint32_t symbol) {
  if (symbol < 144) {
    bits.Code(0x30 + symbol, 8);
  } else if (symbol < 256) {
    bits.Code(0x190 + symbol - 144, 9);
  } else if (symbol < 280) {
    bits.Code(symbol - 256, 7);
  } else {
    bits.Code(0xC0 + symbol - 280, 8);
  }
}

constexpr std::array<uint16_t, 29> kLengthBase = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,
                                                  15, 17, 19, 23, 27, 31, 35, 43, 51,  59,
                                                  67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<uint8_t, 29> kLengthExtra = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                  2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<uint16_t, 30> kDistanceBase = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<uint8_t, 30> kDistanceExtra = {0, 0, 0, 0, 1, 1, 2,  2,  3,  3,
                                                    4, 4, 5, 5, 6, 6, 7,  7,  8,  8,
                                                    9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void Match(BitWriter& bits, uint32_t length, uint32_t distance) {
  size_t code = kLengthBase.size() - 1;
  while (kLengthBase[code] > length) {
    --code;
  }
  Literal(bits, 257 + uint32_t(code));
  bits.Bits(length - kLengthBase[code], kLengthExtra[code]);
  code = kDistanceBase.size() - 1;
  while (kDistanceBase[code] > distance) {
    --code;
  }
  bits.Code(uint32_t(code), 5);
  bits.Bits(distance - kDistanceBase[code], kDistanceExtra[code]);
}

std::vector<uint8_t> Zlib(const std::vector<uint8_t>& data) {
  constexpr uint32_t kWindow = 32768;
  constexpr uint32_t kMaxMatch = 258;
  constexpr uint32_t kHashBits = 16;
  std::vector<uint8_t> out = {0x78, 0x01};
  BitWriter bits(out);
  bits.Bits(1, 1);  // final block
  bits.Bits(1, 2);  // fixed Huffman codes
  std::vector<int32_t> head(size_t(1) << kHashBits, -1);
  const auto hash = [&](size_t i) {
    return ((uint32_t(data[i]) << 16 | uint32_t(data[i + 1]) << 8 | data[i + 2]) * 2654435761u) >>
           (32 - kHashBits);
  };
  size_t i = 0;
  while (i < data.size()) {
    uint32_t best_length = 0, best_distance = 0;
    if (i + 3 <= data.size()) {
      const uint32_t h = hash(i);
      const int32_t candidate = head[h];
      head[h] = int32_t(i);
      if (candidate >= 0 && i - size_t(candidate) <= kWindow) {
        const size_t limit = std::min<size_t>(kMaxMatch, data.size() - i);
        size_t length = 0;
        while (length < limit && data[size_t(candidate) + length] == data[i + length]) {
          ++length;
        }
        if (length >= 3) {
          best_length = uint32_t(length);
          best_distance = uint32_t(i - size_t(candidate));
        }
      }
    }
    if (best_length) {
      Match(bits, best_length, best_distance);
      // Index the skipped positions too, so later matches can find them.
      for (size_t j = i + 1; j < i + best_length && j + 3 <= data.size(); ++j) {
        head[hash(j)] = int32_t(j);
      }
      i += best_length;
    } else {
      Literal(bits, data[i]);
      ++i;
    }
  }
  Literal(bits, 256);  // end of block
  bits.Flush();
  uint32_t a = 1, b = 0;
  for (uint8_t byte : data) {
    a = (a + byte) % 65521;
    b = (b + a) % 65521;
  }
  const uint32_t adler = b << 16 | a;
  for (int shift = 24; shift >= 0; shift -= 8) {
    out.push_back(uint8_t(adler >> shift));
  }
  return out;
}

uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> entries{};
    for (uint32_t n = 0; n < 256; ++n) {
      uint32_t c = n;
      for (int k = 0; k < 8; ++k) {
        c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      }
      entries[n] = c;
    }
    return entries;
  }();
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return ~crc;
}

void Chunk(std::vector<uint8_t>& png, const char (&type)[5], const std::vector<uint8_t>& body) {
  const uint32_t size = uint32_t(body.size());
  for (int shift = 24; shift >= 0; shift -= 8) {
    png.push_back(uint8_t(size >> shift));
  }
  const size_t type_offset = png.size();
  png.insert(png.end(), type, type + 4);
  png.insert(png.end(), body.begin(), body.end());
  const uint32_t crc = Crc32(png.data() + type_offset, 4 + body.size());
  for (int shift = 24; shift >= 0; shift -= 8) {
    png.push_back(uint8_t(crc >> shift));
  }
}

}  // namespace

std::vector<uint8_t> EncodePng(const uint8_t* pixels, uint32_t width, uint32_t height,
                               size_t stride, uint32_t bytes_per_pixel) {
  // Each row: the filter that leaves the smallest sum of absolute values.
  const size_t row_bytes = size_t(width) * 3;
  std::vector<uint8_t> filtered;
  filtered.reserve((row_bytes + 1) * height);
  std::vector<uint8_t> previous(row_bytes, 0), current(row_bytes), candidate(row_bytes),
      best(row_bytes);
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* row = pixels + size_t(y) * stride;
    for (uint32_t x = 0; x < width; ++x) {
      std::copy_n(row + size_t(x) * bytes_per_pixel, 3, current.begin() + size_t(x) * 3);
    }
    uint8_t best_filter = 0;
    uint64_t best_score = UINT64_MAX;
    for (uint8_t filter = 0; filter < 3; ++filter) {
      uint64_t score = 0;
      for (size_t i = 0; i < row_bytes; ++i) {
        uint8_t predictor = 0;
        if (filter == 1 && i >= 3) predictor = current[i - 3];
        if (filter == 2) predictor = previous[i];
        candidate[i] = uint8_t(current[i] - predictor);
        score += uint64_t(std::abs(int(int8_t(candidate[i]))));
      }
      if (score < best_score) {
        best_score = score;
        best_filter = filter;
        best.swap(candidate);
      }
    }
    filtered.push_back(best_filter);
    filtered.insert(filtered.end(), best.begin(), best.end());
    previous.swap(current);
  }

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
  std::vector<uint8_t> header;
  for (uint32_t value : {width, height}) {
    for (int shift = 24; shift >= 0; shift -= 8) {
      header.push_back(uint8_t(value >> shift));
    }
  }
  header.insert(header.end(), {8, 2, 0, 0, 0});  // 8-bit RGB, deflate, no interlace
  Chunk(png, "IHDR", header);
  Chunk(png, "IDAT", Zlib(filtered));
  Chunk(png, "IEND", {});
  return png;
}

}  // namespace pinyon_shift::ui
