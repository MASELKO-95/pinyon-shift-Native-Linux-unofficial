#include "ui/ui_strings.h"

#include <map>
#include <mutex>

namespace pinyon_shift::ui {
namespace {

std::mutex g_mutex;
// table (lower case) -> key -> text
std::map<std::string, std::map<uint16_t, std::u16string>, std::less<>> g_strings;

std::string Lower(std::string_view text) {
  std::string lower(text);
  for (char& c : lower) {
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
  }
  return lower;
}

// The table name at the end of a path ("game:\media\...\PauseMenu.str").
std::string TableName(std::string_view path) {
  const size_t slash = path.find_last_of("\\/:");
  return Lower(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

}  // namespace

void SetUiString(std::string_view table, uint16_t key, std::u16string text, bool replace) {
  std::lock_guard lock(g_mutex);
  auto& strings = g_strings[TableName(table)];
  if (replace || !strings.contains(key)) strings[key] = std::move(text);
}

std::vector<UiStringOverride> UiStringsFor(std::string_view path) {
  std::vector<UiStringOverride> overrides;
  std::lock_guard lock(g_mutex);
  const auto table = g_strings.find(TableName(path));
  if (table == g_strings.end()) return overrides;
  for (const auto& [key, text] : table->second) overrides.push_back({key, text});
  return overrides;
}

uint32_t StringTableSlack(std::string_view path) {
  uint32_t slack = 0;
  for (const auto& entry : UiStringsFor(path)) {
    slack += 2u * uint32_t(entry.text.size() + 1u);
  }
  // Room to align the first appended string to a character boundary.
  return slack ? slack + 2u : 0u;
}

std::u16string Utf8ToUtf16(std::string_view text) {
  std::u16string out;
  for (size_t i = 0; i < text.size();) {
    const auto byte = static_cast<unsigned char>(text[i]);
    uint32_t code = 0xFFFD;
    size_t length = 1;
    if (byte < 0x80) {
      code = byte;
    } else if ((byte >> 5) == 0x6 && i + 1 < text.size()) {
      code = ((byte & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
      length = 2;
    } else if ((byte >> 4) == 0xE && i + 2 < text.size()) {
      code = ((byte & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
      length = 3;
    } else if ((byte >> 3) == 0x1E && i + 3 < text.size()) {
      code = ((byte & 0x07u) << 18) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 12) |
             ((static_cast<unsigned char>(text[i + 2]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(text[i + 3]) & 0x3Fu);
      length = 4;
    }
    if (code >= 0x10000u && code <= 0x10FFFFu) {
      code -= 0x10000u;
      out.push_back(char16_t(0xD800u + (code >> 10)));
      out.push_back(char16_t(0xDC00u + (code & 0x3FFu)));
    } else {
      out.push_back(char16_t(code > 0xFFFFu ? 0xFFFDu : code));
    }
    i += length;
  }
  return out;
}

}  // namespace pinyon_shift::ui
