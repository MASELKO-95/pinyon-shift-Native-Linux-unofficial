#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pinyon_shift::ui {

// Replacement text for entries of the title's string tables (NP-11.2), keyed
// by the table's file name ("pausemenu.str", any case) and the entry's 16-bit
// key (tools/fh1-strings.py lists them). A replacement applies when the title
// next loads the table and may be longer than the stock text: the loader's
// buffer gets room for it (StringTableSlack) and the entry is pointed at a
// copy appended to the string pool.
// With `replace` false an existing replacement is kept (the host's defaults
// yield to mods').
void SetUiString(std::string_view table, uint16_t key, std::u16string text, bool replace = true);

struct UiStringOverride {
  uint16_t key = 0;
  std::u16string text;
};
// The replacements for the table loaded from `path` (a VFS path ending in the
// table's name), in key order.
std::vector<UiStringOverride> UiStringsFor(std::string_view path);

// Extra bytes the loader must allocate for `path`'s string chunk so every
// replacement fits after the stock pool.
uint32_t StringTableSlack(std::string_view path);

// UTF-8 to UTF-16; invalid sequences become U+FFFD.
std::u16string Utf8ToUtf16(std::string_view text);

}  // namespace pinyon_shift::ui
