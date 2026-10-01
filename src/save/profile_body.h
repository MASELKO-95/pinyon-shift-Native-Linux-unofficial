#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pinyon_shift::save {

// The plaintext ForzaProfile body the title encrypts at 0x82C666D0 begins
// with a self-describing section: a big-endian field count, then per field
// [u32 name length][name][u32 0x20][u32 0][u8 type][value], where a struct
// (type 0x0F) holds its own count and fields. Class-serialised states and
// 0xBB padding follow; they are not described here and are left alone.
enum class FieldType : uint8_t {
  kBool = 0x00,
  kUInt8 = 0x01,
  kUInt32 = 0x03,
  kUInt64 = 0x04,
  kInt32 = 0x07,
  kFloat = 0x09,
  kStruct = 0x0F,
};

struct ProfileField {
  size_t offset = 0;  // of the value in the body
  FieldType type = FieldType::kUInt32;
  size_t size = 0;    // of the value in bytes
};

// Finds a field by its slash-separated path ("Main/Credits"); nullopt when
// the section does not parse or has no such field.
std::optional<ProfileField> FindProfileField(const uint8_t* body, size_t size,
                                             std::string_view path);

// Reads a 32-bit integer field (UInt32 or Int32), big-endian.
std::optional<uint32_t> ReadProfileU32(const uint8_t* body, size_t size, std::string_view path);

}  // namespace pinyon_shift::save
