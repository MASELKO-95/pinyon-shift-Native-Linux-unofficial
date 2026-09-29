#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace pinyon_shift::hostui {

// An 8-bit coverage bitmap. `left` and `top` place its top-left texel
// relative to the pen position on the baseline, in pixels with y down.
struct GlyphBitmap {
  int width = 0;
  int height = 0;
  int left = 0;
  int top = 0;
  std::vector<uint8_t> alpha;
};

// A FontCompiler vector font (`*_vector_aa.dt`, CAFF asset "vfont") from the
// title's Fonts.zip. Glyphs are triangle meshes in ems; docs/UI_ASSETS.md
// describes the format and the coverage rule the title's VectorFont shader
// applies, which Rasterize reproduces on the CPU.
class VectorFont {
 public:
  struct Glyph {
    uint32_t code_point = 0;
    float advance = 0.0f;   // ems
    float offset_x = 0.0f;  // ems, added to every vertex
    uint32_t first_vertex = 0;
    uint32_t vertex_count = 0;
    uint32_t first_index = 0;
    uint32_t triangle_count = 0;
  };

  static std::optional<VectorFont> Parse(std::span<const uint8_t> data);

  const Glyph* Find(uint32_t code_point) const;
  // Line metrics in ems: the design ascent and descent over their sum.
  float ascent() const { return ascent_; }
  float descent() const { return descent_; }
  size_t glyph_count() const { return glyphs_.size(); }

  // Coverage of `glyph` at `pixels_per_em`, sampled at pixel centres.
  GlyphBitmap Rasterize(const Glyph& glyph, float pixels_per_em) const;

 private:
  struct Vertex {
    float x;  // |x| + offset, ems
    float y;  // ems, up
    float u;
    float v;
    float s;  // +1 inside the outline, -1 outside
  };

  std::vector<Glyph> glyphs_;
  std::unordered_map<uint32_t, size_t> by_code_point_;
  std::vector<uint16_t> raw_vertices_;  // x, y, z, w per vertex, host order
  std::vector<uint16_t> indices_;
  float ascent_ = 0.75f;
  float descent_ = 0.25f;
};

}  // namespace pinyon_shift::hostui
