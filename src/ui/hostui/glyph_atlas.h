#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ui/hostui/vector_font.h"

namespace pinyon_shift::hostui {

std::u32string DecodeUtf8(std::string_view text);

// Glyphs of one vector font at one pixel size, packed into a white RGBA
// texture whose alpha is the coverage. Glyphs are added on demand; the owner
// re-uploads the texture when Prepare reports a change.
class GlyphAtlas {
 public:
  struct Entry {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int left = 0;  // from the pen position, pixels
    int top = 0;
    float advance = 0.0f;  // pixels
  };

  GlyphAtlas(const VectorFont& font, float pixels_per_em);

  // Rasterizes any glyphs of `text` not in the atlas yet. Code points the
  // font lacks fall back to '?'. Returns whether the texture changed.
  bool Prepare(std::u32string_view text);
  const Entry* Find(char32_t code_point) const;

  float pixels_per_em() const { return pixels_per_em_; }
  float ascent() const { return font_.ascent() * pixels_per_em_; }
  float descent() const { return font_.descent() * pixels_per_em_; }
  float Measure(std::u32string_view text) const;

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  const std::vector<uint8_t>& rgba() const { return rgba_; }

 private:
  bool Add(char32_t code_point);
  void Grow(uint32_t minimum_height);

  const VectorFont& font_;
  float pixels_per_em_;
  uint32_t width_;
  uint32_t height_ = 0;
  std::vector<uint8_t> rgba_;
  std::unordered_map<char32_t, Entry> entries_;
  // Shelf packing.
  int shelf_x_ = 1;
  int shelf_y_ = 1;
  int shelf_height_ = 0;
};

}  // namespace pinyon_shift::hostui
