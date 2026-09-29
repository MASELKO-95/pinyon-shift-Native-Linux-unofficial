#include "save/profile_body.h"

#include <string>

namespace pinyon_shift::save {
namespace {

constexpr uint32_t kMaxDepth = 8;

uint32_t LoadBe32(const uint8_t* bytes) {
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) |
         uint32_t(bytes[3]);
}

std::optional<size_t> ValueSize(uint8_t type) {
  switch (static_cast<FieldType>(type)) {
    case FieldType::kBool:
    case FieldType::kUInt8:
      return 1;
    case FieldType::kUInt32:
    case FieldType::kInt32:
    case FieldType::kFloat:
      return 4;
    case FieldType::kUInt64:
      return 8;
    default:
      return std::nullopt;
  }
}

class Parser {
 public:
  Parser(const uint8_t* body, size_t size, std::string_view path)
      : body_(body), size_(size), path_(path) {}

  // Walks `count` fields from `offset`; returns the offset after them, or
  // nullopt on a malformed section. Stops early once the field is found.
  std::optional<size_t> Fields(size_t offset, uint32_t count, const std::string& prefix,
                               uint32_t depth) {
    if (depth > kMaxDepth) return std::nullopt;
    for (uint32_t i = 0; i < count && !found_; ++i) {
      if (offset + 4 > size_) return std::nullopt;
      const uint32_t length = LoadBe32(body_ + offset);
      if (length == 0 || length > 256 || offset + 4 + length + 9 > size_) return std::nullopt;
      const std::string name(reinterpret_cast<const char*>(body_ + offset + 4), length);
      offset += 4 + length;
      if (LoadBe32(body_ + offset) != 0x20 || LoadBe32(body_ + offset + 4) != 0) {
        return std::nullopt;
      }
      const uint8_t type = body_[offset + 8];
      offset += 9;
      const std::string path = prefix.empty() ? name : prefix + "/" + name;
      if (type == static_cast<uint8_t>(FieldType::kStruct)) {
        if (offset + 4 > size_) return std::nullopt;
        const uint32_t children = LoadBe32(body_ + offset);
        const auto end = Fields(offset + 4, children, path, depth + 1);
        if (!end) return std::nullopt;
        offset = *end;
        continue;
      }
      const auto value_size = ValueSize(type);
      if (!value_size || offset + *value_size > size_) return std::nullopt;
      if (path == path_) {
        found_ = ProfileField{offset, static_cast<FieldType>(type), *value_size};
      }
      offset += *value_size;
    }
    return offset;
  }

  std::optional<ProfileField> found() const { return found_; }

 private:
  const uint8_t* body_;
  size_t size_;
  std::string_view path_;
  std::optional<ProfileField> found_;
};

}  // namespace

std::optional<ProfileField> FindProfileField(const uint8_t* body, size_t size,
                                             std::string_view path) {
  if (!body || size < 4) return std::nullopt;
  Parser parser(body, size, path);
  if (!parser.Fields(4, LoadBe32(body), {}, 0)) return std::nullopt;
  return parser.found();
}

std::optional<uint32_t> ReadProfileU32(const uint8_t* body, size_t size, std::string_view path) {
  const auto field = FindProfileField(body, size, path);
  if (!field || (field->type != FieldType::kUInt32 && field->type != FieldType::kInt32)) {
    return std::nullopt;
  }
  return LoadBe32(body + field->offset);
}

}  // namespace pinyon_shift::save
