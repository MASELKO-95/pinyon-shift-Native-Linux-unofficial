#include "ui/hostui/fh1_archive.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

#include <rex/system/lzx.h>

namespace pinyon_shift::hostui {
namespace {

constexpr uint32_t kLocalHeaderSignature = 0x04034B50;
constexpr uint32_t kCentralHeaderSignature = 0x02014B50;
constexpr uint32_t kEndOfDirectorySignature = 0x06054B50;
constexpr size_t kEndOfDirectorySize = 22;
constexpr size_t kLocalHeaderSize = 30;
constexpr size_t kCentralHeaderSize = 46;
constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodXmem = 21;

uint16_t Read16(const uint8_t* data) { return uint16_t(data[0] | data[1] << 8); }

uint32_t Read32(const uint8_t* data) {
  return uint32_t(data[0]) | uint32_t(data[1]) << 8 | uint32_t(data[2]) << 16 |
         uint32_t(data[3]) << 24;
}

std::string Lowercase(std::string_view text) {
  std::string lowered(text);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });
  return lowered;
}

bool ReadRange(std::ifstream& stream, uint64_t offset, size_t size, std::vector<uint8_t>& out) {
  out.resize(size);
  stream.clear();
  stream.seekg(std::streamoff(offset));
  return bool(stream.read(reinterpret_cast<char*>(out.data()), std::streamsize(size)));
}

}  // namespace

bool ParseXmemPayload(std::span<const uint8_t> compressed, size_t uncompressed_size,
                      std::vector<uint8_t>& payload) {
  payload.clear();
  size_t offset = 0;
  size_t produced = 0;
  while (produced < uncompressed_size) {
    size_t block_compressed;
    size_t block_uncompressed;
    if (offset < compressed.size() && compressed[offset] == 0xFF) {
      if (compressed.size() - offset < 5) {
        return false;
      }
      block_uncompressed = size_t(compressed[offset + 1]) << 8 | compressed[offset + 2];
      block_compressed = size_t(compressed[offset + 3]) << 8 | compressed[offset + 4];
      offset += 5;
    } else {
      if (compressed.size() - offset < 2) {
        return false;
      }
      block_uncompressed = std::min<size_t>(0x8000, uncompressed_size - produced);
      block_compressed = size_t(compressed[offset]) << 8 | compressed[offset + 1];
      offset += 2;
    }
    if (!block_compressed || !block_uncompressed ||
        block_compressed > compressed.size() - offset ||
        block_uncompressed > uncompressed_size - produced) {
      return false;
    }
    payload.insert(payload.end(), compressed.begin() + offset,
                   compressed.begin() + offset + block_compressed);
    offset += block_compressed;
    produced += block_uncompressed;
  }
  return true;
}

std::optional<std::vector<uint8_t>> DecompressXmem(std::span<const uint8_t> compressed,
                                                   size_t uncompressed_size) {
  std::vector<uint8_t> payload;
  if (!ParseXmemPayload(compressed, uncompressed_size, payload)) {
    return std::nullopt;
  }
  std::vector<uint8_t> output(uncompressed_size);
  for (uint32_t window_size : {uint32_t(1) << 16, uint32_t(1) << 17, uint32_t(1) << 15}) {
    if (!lzx_decompress(payload.data(), payload.size(), output.data(), output.size(),
                        window_size, nullptr, 0)) {
      return output;
    }
  }
  return std::nullopt;
}

uint32_t Crc32(std::span<const uint8_t> data) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> values{};
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t value = i;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1) ? 0xEDB88320u ^ (value >> 1) : value >> 1;
      }
      values[i] = value;
    }
    return values;
  }();
  uint32_t crc = 0xFFFFFFFFu;
  for (uint8_t byte : data) {
    crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

std::optional<Fh1Archive> Fh1Archive::Open(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return std::nullopt;
  }
  stream.seekg(0, std::ios::end);
  const uint64_t file_size = uint64_t(stream.tellg());
  if (file_size < kEndOfDirectorySize) {
    return std::nullopt;
  }
  // The end-of-directory record is followed by at most a 64 KiB comment.
  const size_t tail_size = size_t(std::min<uint64_t>(file_size, kEndOfDirectorySize + 0xFFFF));
  std::vector<uint8_t> tail;
  if (!ReadRange(stream, file_size - tail_size, tail_size, tail)) {
    return std::nullopt;
  }
  size_t end_record = SIZE_MAX;
  for (size_t at = tail_size - kEndOfDirectorySize + 1; at-- > 0;) {
    if (Read32(&tail[at]) == kEndOfDirectorySignature) {
      end_record = at;
      break;
    }
  }
  if (end_record == SIZE_MAX) {
    return std::nullopt;
  }
  const uint16_t entry_count = Read16(&tail[end_record + 10]);
  const uint32_t directory_size = Read32(&tail[end_record + 12]);
  const uint32_t directory_offset = Read32(&tail[end_record + 16]);
  if (uint64_t(directory_offset) + directory_size > file_size) {
    return std::nullopt;
  }
  std::vector<uint8_t> directory;
  if (!ReadRange(stream, directory_offset, directory_size, directory)) {
    return std::nullopt;
  }

  Fh1Archive archive;
  archive.path_ = path;
  size_t at = 0;
  for (uint16_t index = 0; index < entry_count; ++index) {
    if (directory.size() - at < kCentralHeaderSize ||
        Read32(&directory[at]) != kCentralHeaderSignature) {
      return std::nullopt;
    }
    const uint8_t* header = &directory[at];
    const size_t name_size = Read16(header + 28);
    const size_t variable_size = name_size + Read16(header + 30) + Read16(header + 32);
    if (directory.size() - at - kCentralHeaderSize < variable_size) {
      return std::nullopt;
    }
    Member member;
    member.method = Read16(header + 10);
    member.crc32 = Read32(header + 16);
    member.compressed_size = Read32(header + 20);
    member.size = Read32(header + 24);
    member.header_offset = Read32(header + 42);
    std::string_view name(reinterpret_cast<const char*>(header + kCentralHeaderSize), name_size);
    archive.members_.insert_or_assign(Lowercase(name), member);
    at += kCentralHeaderSize + variable_size;
  }
  return archive;
}

bool Fh1Archive::Contains(std::string_view name) const {
  return members_.contains(Lowercase(name));
}

std::optional<std::vector<uint8_t>> Fh1Archive::Read(std::string_view name) const {
  auto found = members_.find(Lowercase(name));
  if (found == members_.end()) {
    return std::nullopt;
  }
  const Member& member = found->second;
  if (member.size > kMaximumMemberSize || member.compressed_size > kMaximumMemberSize ||
      (member.method != kMethodStored && member.method != kMethodXmem)) {
    return std::nullopt;
  }
  std::ifstream stream(path_, std::ios::binary);
  std::vector<uint8_t> local;
  if (!stream || !ReadRange(stream, member.header_offset, kLocalHeaderSize, local)) {
    return std::nullopt;
  }
  // Some FH1 archives omit local headers; the directory offset is then the
  // payload itself.
  uint64_t payload_offset = member.header_offset;
  if (Read32(local.data()) == kLocalHeaderSignature) {
    payload_offset += kLocalHeaderSize + Read16(&local[26]) + Read16(&local[28]);
  }
  std::vector<uint8_t> compressed;
  if (!ReadRange(stream, payload_offset, member.compressed_size, compressed)) {
    return std::nullopt;
  }
  std::optional<std::vector<uint8_t>> data;
  if (member.method == kMethodStored) {
    data = std::move(compressed);
  } else {
    data = DecompressXmem(compressed, member.size);
  }
  if (!data || data->size() != member.size || Crc32(*data) != member.crc32) {
    return std::nullopt;
  }
  return data;
}

}  // namespace pinyon_shift::hostui
