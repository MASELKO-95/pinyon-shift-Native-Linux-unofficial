#include "ui/hostui/vector_font.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace pinyon_shift::hostui {
namespace {

constexpr size_t kDataOffset = 0x190;
constexpr size_t kGlyphRecordSize = 40;

uint32_t ReadBe32(std::span<const uint8_t> data, size_t at) {
  return uint32_t(data[at]) << 24 | uint32_t(data[at + 1]) << 16 | uint32_t(data[at + 2]) << 8 |
         uint32_t(data[at + 3]);
}

uint16_t ReadBe16(std::span<const uint8_t> data, size_t at) {
  return uint16_t(data[at] << 8 | data[at + 1]);
}

float ReadBeFloat(std::span<const uint8_t> data, size_t at) {
  return std::bit_cast<float>(ReadBe32(data, at));
}

float HalfToFloat(uint16_t half) {
  const float sign = (half & 0x8000) ? -1.0f : 1.0f;
  const int exponent = (half >> 10) & 0x1F;
  const int mantissa = half & 0x3FF;
  if (!exponent) {
    return sign * std::ldexp(float(mantissa), -24);
  }
  if (exponent == 31) {
    return mantissa ? std::numeric_limits<float>::quiet_NaN()
                    : sign * std::numeric_limits<float>::infinity();
  }
  return sign * std::ldexp(float(mantissa | 0x400), exponent - 25);
}

}  // namespace

std::optional<VectorFont> VectorFont::Parse(std::span<const uint8_t> data) {
  if (data.size() < kDataOffset + 0x400 || std::memcmp(data.data(), "CAFF", 4) != 0 ||
      std::memcmp(data.data() + kDataOffset, "vfont", 6) != 0) {
    return std::nullopt;
  }
  const size_t gpu = kDataOffset + ReadBe32(data, 0x60);
  const size_t glyph_count = ReadBe32(data, kDataOffset + 0x34C);
  const size_t hash_offset = ReadBe32(data, kDataOffset + 0x35C);
  const size_t hash_size = ReadBe32(data, kDataOffset + 0x360);
  const size_t after_hash = kDataOffset + hash_offset + 4 * hash_size;
  if (gpu > data.size() || after_hash + 8 > gpu || glyph_count > 0x10000) {
    return std::nullopt;
  }
  const size_t metrics = kDataOffset + ReadBe32(data, after_hash);
  const size_t records = kDataOffset + ReadBe32(data, after_hash + 4);
  if (metrics + 0x2C > gpu || records + glyph_count * kGlyphRecordSize > gpu) {
    return std::nullopt;
  }
  const size_t vertex_bytes = ReadBe32(data, metrics + 8);
  const size_t index_count = ReadBe32(data, metrics + 0x10);
  if (vertex_bytes % 8 || vertex_bytes > data.size() - gpu ||
      index_count * 2 > data.size() - gpu - vertex_bytes) {
    return std::nullopt;
  }

  VectorFont font;
  const auto ascent = int32_t(ReadBe32(data, metrics + 0x24));
  const auto descent = int32_t(ReadBe32(data, metrics + 0x28));
  if (ascent > 0 && descent > 0) {
    font.ascent_ = float(ascent) / float(ascent + descent);
    font.descent_ = float(descent) / float(ascent + descent);
  }
  font.raw_vertices_.resize(vertex_bytes / 2);
  for (size_t i = 0; i < font.raw_vertices_.size(); ++i) {
    font.raw_vertices_[i] = ReadBe16(data, gpu + 2 * i);
  }
  font.indices_.resize(index_count);
  for (size_t i = 0; i < index_count; ++i) {
    font.indices_[i] = ReadBe16(data, gpu + vertex_bytes + 2 * i);
  }
  font.glyphs_.reserve(glyph_count);
  for (size_t i = 0; i < glyph_count; ++i) {
    const size_t record = records + i * kGlyphRecordSize;
    Glyph glyph;
    glyph.code_point = ReadBe32(data, record);
    glyph.advance = ReadBeFloat(data, record + 4);
    const uint32_t vertex_offset = ReadBe32(data, record + 12);
    glyph.vertex_count = ReadBe32(data, record + 16);
    glyph.first_index = ReadBe32(data, record + 20);
    glyph.triangle_count = ReadBe32(data, record + 24);
    glyph.offset_x = ReadBeFloat(data, record + 28);
    glyph.first_vertex = vertex_offset / 8;
    if (vertex_offset % 8 || size_t(glyph.first_vertex) + glyph.vertex_count > vertex_bytes / 8 ||
        size_t(glyph.first_index) + 3 * size_t(glyph.triangle_count) > index_count ||
        !std::isfinite(glyph.advance) || !std::isfinite(glyph.offset_x)) {
      return std::nullopt;
    }
    for (uint32_t t = 0; t < 3 * glyph.triangle_count; ++t) {
      if (font.indices_[glyph.first_index + t] >= glyph.vertex_count) {
        return std::nullopt;
      }
    }
    font.by_code_point_.emplace(glyph.code_point, font.glyphs_.size());
    font.glyphs_.push_back(glyph);
  }
  return font;
}

const VectorFont::Glyph* VectorFont::Find(uint32_t code_point) const {
  auto found = by_code_point_.find(code_point);
  return found == by_code_point_.end() ? nullptr : &glyphs_[found->second];
}

GlyphBitmap VectorFont::Rasterize(const Glyph& glyph, float pixels_per_em) const {
  GlyphBitmap bitmap;
  if (!glyph.triangle_count || !(pixels_per_em > 0.0f)) {
    return bitmap;
  }
  // The vertex shader places a vertex at (|x| + offset, y); the sign of x
  // marks triangles inside (+) or outside (-) the outline, and z, w are the
  // curve coordinates (u, v).
  std::vector<Vertex> vertices(glyph.vertex_count);
  float min_x = std::numeric_limits<float>::max(), max_x = -min_x;
  float min_y = min_x, max_y = -min_x;
  for (uint32_t i = 0; i < glyph.vertex_count; ++i) {
    const uint16_t* raw = &raw_vertices_[4 * size_t(glyph.first_vertex + i)];
    const float x = HalfToFloat(raw[0]);
    Vertex& vertex = vertices[i];
    vertex.x = (std::fabs(x) + glyph.offset_x) * pixels_per_em;
    vertex.y = -HalfToFloat(raw[1]) * pixels_per_em;
    vertex.u = HalfToFloat(raw[2]);
    vertex.v = HalfToFloat(raw[3]);
    vertex.s = x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f);
    min_x = std::min(min_x, vertex.x);
    max_x = std::max(max_x, vertex.x);
    min_y = std::min(min_y, vertex.y);
    max_y = std::max(max_y, vertex.y);
  }
  const int left = int(std::floor(min_x)) - 1;
  const int top = int(std::floor(min_y)) - 1;
  const int width = int(std::ceil(max_x)) - left + 2;
  const int height = int(std::ceil(max_y)) - top + 2;
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
    return bitmap;
  }
  std::vector<float> coverage(size_t(width) * height, 0.0f);

  for (uint32_t t = 0; t < glyph.triangle_count; ++t) {
    const Vertex& a = vertices[indices_[glyph.first_index + 3 * t]];
    const Vertex& b = vertices[indices_[glyph.first_index + 3 * t + 1]];
    const Vertex& c = vertices[indices_[glyph.first_index + 3 * t + 2]];
    const float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
    if (area == 0.0f) {
      continue;
    }
    const float inverse_area = 1.0f / area;
    // Screen-space gradients of u and v, constant over the triangle; the
    // title takes them per 2x2 quad with the same result on a plane.
    const auto gradient = [&](float q0, float q1, float q2, float& gx, float& gy) {
      gx = ((q1 - q0) * (c.y - a.y) - (q2 - q0) * (b.y - a.y)) * inverse_area;
      gy = ((q2 - q0) * (b.x - a.x) - (q1 - q0) * (c.x - a.x)) * inverse_area;
    };
    float ux, uy, vx, vy;
    gradient(a.u, b.u, c.u, ux, uy);
    gradient(a.v, b.v, c.v, vx, vy);
    const int x0 = std::max(0, int(std::floor(std::min({a.x, b.x, c.x}))) - left);
    const int x1 = std::min(width - 1, int(std::ceil(std::max({a.x, b.x, c.x}))) - left);
    const int y0 = std::max(0, int(std::floor(std::min({a.y, b.y, c.y}))) - top);
    const int y1 = std::min(height - 1, int(std::ceil(std::max({a.y, b.y, c.y}))) - top);
    for (int py = y0; py <= y1; ++py) {
      const float y = float(py + top) + 0.5f;
      for (int px = x0; px <= x1; ++px) {
        const float x = float(px + left) + 0.5f;
        const float w0 = ((b.x - x) * (c.y - y) - (c.x - x) * (b.y - y)) * inverse_area;
        const float w1 = ((c.x - x) * (a.y - y) - (a.x - x) * (c.y - y)) * inverse_area;
        const float w2 = 1.0f - w0 - w1;
        if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) {
          continue;
        }
        const float u = w0 * a.u + w1 * b.u + w2 * c.u;
        const float v = w0 * a.v + w1 * b.v + w2 * c.v;
        const float s = w0 * a.s + w1 * b.s + w2 * c.s;
        // f = u^2 - v is the implicit curve; f * s over its gradient length
        // is the signed distance to the edge in pixels.
        const float gx = s * (2.0f * u * ux - vx);
        const float gy = s * (2.0f * u * uy - vy);
        const float gradient_length = std::sqrt(gx * gx + gy * gy);
        const float signed_value = (u * u - v) * s;
        // Where u and v are constant (deep inside or far outside) the
        // gradient is zero and only the sign of f * s matters.
        const float distance =
            gradient_length > 0.0f ? signed_value / gradient_length
            : signed_value < 0.0f  ? -std::numeric_limits<float>::max()
            : signed_value > 0.0f  ? std::numeric_limits<float>::max()
                                   : 0.0f;
        const float alpha = std::clamp(0.5f - distance, 0.0f, 1.0f);
        if (alpha > 0.0f) {
          float& value = coverage[size_t(py) * width + px];
          value = alpha + value * (1.0f - alpha);
        }
      }
    }
  }

  // Trim to the covered texels.
  int trim_left = width, trim_right = -1, trim_top = height, trim_bottom = -1;
  for (int py = 0; py < height; ++py) {
    for (int px = 0; px < width; ++px) {
      if (coverage[size_t(py) * width + px] >= 0.5f / 255.0f) {
        trim_left = std::min(trim_left, px);
        trim_right = std::max(trim_right, px);
        trim_top = std::min(trim_top, py);
        trim_bottom = std::max(trim_bottom, py);
      }
    }
  }
  if (trim_right < 0) {
    return bitmap;
  }
  bitmap.width = trim_right - trim_left + 1;
  bitmap.height = trim_bottom - trim_top + 1;
  bitmap.left = left + trim_left;
  bitmap.top = top + trim_top;
  bitmap.alpha.resize(size_t(bitmap.width) * bitmap.height);
  for (int py = 0; py < bitmap.height; ++py) {
    for (int px = 0; px < bitmap.width; ++px) {
      const float value = coverage[size_t(py + trim_top) * width + px + trim_left];
      bitmap.alpha[size_t(py) * bitmap.width + px] = uint8_t(std::lround(value * 255.0f));
    }
  }
  return bitmap;
}

}  // namespace pinyon_shift::hostui
