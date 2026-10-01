#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "save/profile_body.h"

using namespace pinyon_shift::save;

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

void Field(std::vector<uint8_t>& out, const std::string& name, uint8_t type) {
  Be32(out, uint32_t(name.size()));
  out.insert(out.end(), name.begin(), name.end());
  Be32(out, 0x20);
  Be32(out, 0);
  out.push_back(type);
}

// Two top-level structs, like the title's profile: Options (a bool) and Main
// (credits, XP, a float, a byte and a 64-bit time), then an opaque tail.
std::vector<uint8_t> Body() {
  std::vector<uint8_t> body;
  Be32(body, 2);
  Field(body, "Options", 0x0F);
  Be32(body, 1);
  Field(body, "Tutorial", 0x00);
  body.push_back(1);
  Field(body, "Main", 0x0F);
  Be32(body, 5);
  Field(body, "Exposure", 0x09);
  Be32(body, 0x3DCCCCCD);
  Field(body, "SatNav", 0x01);
  body.push_back(1);
  Field(body, "UpTime", 0x04);
  Be32(body, 0);
  Be32(body, 49217825);
  Field(body, "Credits", 0x03);
  Be32(body, 167700);
  Field(body, "XP", 0x07);
  Be32(body, 820);
  for (int i = 0; i < 8; ++i) body.push_back(0xBB);
  return body;
}

void TestFindsNestedFields() {
  const auto body = Body();
  const auto credits = FindProfileField(body.data(), body.size(), "Main/Credits");
  CHECK(credits.has_value());
  CHECK(credits && credits->type == FieldType::kUInt32 && credits->size == 4);
  CHECK(ReadProfileU32(body.data(), body.size(), "Main/Credits") == 167700u);
  CHECK(ReadProfileU32(body.data(), body.size(), "Main/XP") == 820u);
  CHECK(FindProfileField(body.data(), body.size(), "Options/Tutorial")->size == 1);
  CHECK(FindProfileField(body.data(), body.size(), "Main/UpTime")->size == 8);
  // Only 32-bit integers read as numbers.
  CHECK(!ReadProfileU32(body.data(), body.size(), "Main/Exposure"));
  CHECK(!FindProfileField(body.data(), body.size(), "Main/Missing"));
  CHECK(!FindProfileField(body.data(), body.size(), "Credits"));
}

void TestRejectsOtherBodies() {
  auto body = Body();
  // A truncated section.
  CHECK(!FindProfileField(body.data(), 40, "Main/Credits"));
  // A field header that is not [0x20][0].
  body[4 + 4 + 7 + 3] = 0x21;
  CHECK(!FindProfileField(body.data(), body.size(), "Main/Credits"));
  // A body that starts like the title's other secure file.
  const std::vector<uint8_t> other = {0, 0, 0, 1, 0, 0, 0x1F, 0x4D, 0, 0, 0, 0};
  CHECK(!FindProfileField(other.data(), other.size(), "Main/Credits"));
  CHECK(!FindProfileField(nullptr, 0, "Main/Credits"));
}

}  // namespace

int main() {
  TestFindsNestedFields();
  TestRejectsOtherBodies();
  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::printf("profile_body tests passed\n");
  return 0;
}
