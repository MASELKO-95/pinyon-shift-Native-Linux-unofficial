#include "native_renderer/ordered_ui_capture.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <rex/logging.h>
#include <rex/cvar.h>
#include <rex/system/interfaces/graphics.h>

#include "pinyon_shift_diagnostics.h"

namespace pinyon_shift::native_renderer {
namespace {

constexpr uint64_t kMaximumPayloadBytes = 128ull * 1024 * 1024;
constexpr uint64_t kMaximumGeometryBytes = 256ull * 1024 * 1024;
constexpr size_t kMaximumDraws = 256;
static_assert(sizeof(rex::system::GraphicsPreparedDrawTextureFetch) == 36);
static_assert(sizeof(rex::system::GraphicsFinalDrawTextureIdentity) == 56);

using Vertex = OrderedUiVertex;
using Draw = OrderedUiDraw;

struct Frame {
  uint64_t source_frame = 0, payload_bytes = 0, geometry_bytes = 0;
  bool rejected = false, geometry_incomplete = false, state_incomplete = false;
  std::map<uint64_t, Draw> draws;
  std::map<std::pair<uint64_t, uint32_t>, std::vector<uint8_t>> geometry;
  struct VertexInput {
    uint32_t fetch = 0, base = 0, length = 0, stride = 0, type = 0;
    uint32_t snapshot_status = 0, snapshot_length = 0;
    uint64_t snapshot_hash = 0;
    uint64_t blob_hash = 0;
    uint32_t blob_length = 0;
  };
  struct Event {
    char kind = 'D';
    uint32_t surface = 0, color = 0, depth = 0, target_bits = 0;
    uint64_t vertex_shader = 0, pixel_shader = 0;
    uint64_t vertex_specialization = 0, pixel_specialization = 0;
    uint32_t index_count = 0, vertex_fetches = 0, texture_fetches = 0;
    uint32_t primitive = 0, index_type = 0, index_base = 0;
    uint32_t index_length = 0, index_endianness = 0;
    uint32_t index_snapshot_status = 0;
    uint64_t index_snapshot_hash = 0;
    uint64_t index_blob_hash = 0;
    uint32_t index_blob_length = 0;
    std::vector<VertexInput> vertices;
    uint32_t prepared_depth = 0, color_mask = 0;
    uint32_t raster = 0, clip = 0, final_depth = 0;
    uint64_t dynamic_state = 0;
    std::array<uint64_t, 4> vertex_bitmap{}, pixel_bitmap{};
    bool state_bitmap_ready = false, state_snapshot_ready = false;
    std::vector<uint32_t> constants, system, fetch, bool_loop;
    std::array<float, 6> viewport{};
    std::array<int32_t, 4> scissor{};
    std::vector<rex::system::GraphicsFinalDrawTextureIdentity> textures;
    uint32_t final_seen = 0, versioned_textures = 0;
    uint32_t dest_base = 0, dest_pitch = 0;
    uint32_t resolve_width = 0, resolve_height = 0, succeeded = 0;
    OrderedCopyInputs copy;
    uint32_t clear_mode = 0, clear_flags = 0;
    uint32_t stencil_reference = 0, rectangle_count = 0;
    std::array<std::array<int32_t, 4>, 2> bounds{};
    std::array<float, 2> clear_depth{};
    std::array<std::array<float, 4>, 2> clear_color{};
  };
  std::map<uint64_t, Event> events;
};

std::mutex capture_mutex;
std::map<uint64_t, Frame> captured_frames;

Frame& CaptureFrame(uint64_t source_frame) {
  auto& frame = captured_frames[source_frame];
  frame.source_frame = source_frame;
  while (captured_frames.size() > 8) captured_frames.erase(captured_frames.begin());
  return frame;
}

bool CompleteUi(const Frame& frame) {
  return !frame.rejected && !frame.draws.empty() &&
      std::all_of(frame.draws.begin(), frame.draws.end(),
                  [](const auto& entry) {
                    const auto& draw = entry.second;
                    return draw.final_seen &&
                        draw.texture_fetches.size() == draw.textures.size() &&
                        std::all_of(draw.textures.begin(), draw.textures.end(),
                                    [](const auto& texture) {
                                      return texture.allocation_id &&
                                          texture.payload_generation &&
                                          !texture.outdated_mask;
                                    });
                  }) &&
      frame.draws.rbegin()->first - frame.draws.begin()->first + 1 ==
          frame.draws.size();
}

void Reject(uint64_t frame) {
  std::lock_guard lock(capture_mutex);
  CaptureFrame(frame).rejected = true;
}

template <typename T>
void Write(std::ofstream& stream, const T& value) {
  stream.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <typename T, size_t N>
void Write(std::ofstream& stream, const std::array<T, N>& value) {
  stream.write(reinterpret_cast<const char*>(value.data()), sizeof(value));
}

void WriteBytes(std::ofstream& stream, const std::vector<uint8_t>& bytes) {
  if (!bytes.empty())
    stream.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void WriteWords(std::ofstream& stream, const std::vector<uint32_t>& words) {
  stream.write(reinterpret_cast<const char*>(words.data()),
               words.size() * sizeof(uint32_t));
}

uint64_t ArtifactHash(const uint8_t* bytes, size_t length) {
  uint64_t hash = 14695981039346656037ull;
  for (size_t i = 0; i < length; ++i)
    hash = (hash ^ bytes[i]) * 1099511628211ull;
  return hash;
}

uint64_t ArtifactHash(const std::vector<uint8_t>& bytes) {
  return ArtifactHash(bytes.data(), bytes.size());
}

std::optional<uint64_t> RecordGeometry(Frame& frame, const uint8_t* bytes,
                                       uint32_t length) {
  if (!bytes || !length) {
    frame.geometry_incomplete = true;
    return std::nullopt;
  }
  const uint64_t hash = ArtifactHash(bytes, length);
  const auto key = std::pair{hash, length};
  if (const auto found = frame.geometry.find(key);
      found != frame.geometry.end()) {
    if (!std::equal(found->second.begin(), found->second.end(), bytes))
      frame.rejected = true;
    return hash;
  }
  if (length > kMaximumGeometryBytes - frame.geometry_bytes) {
    frame.geometry_incomplete = true;
    return std::nullopt;
  }
  frame.geometry.emplace(key, std::vector<uint8_t>(bytes, bytes + length));
  frame.geometry_bytes += length;
  return hash;
}

}  // namespace

std::optional<std::vector<OrderedFrameOperation>> SnapshotOrderedFrameOperations(
    uint64_t source_frame) {
  std::lock_guard lock(capture_mutex);
  const auto found = captured_frames.find(source_frame);
  if (found == captured_frames.end() || found->second.rejected ||
      found->second.events.empty())
    return std::nullopt;
  std::vector<OrderedFrameOperation> operations;
  operations.reserve(found->second.events.size());
  for (const auto& [sequence, event] : found->second.events) {
    OrderedFrameOperation operation{sequence, event.kind, event.surface,
                                    event.color, event.depth, event.target_bits};
    operation.dest_base = event.dest_base;
    operation.dest_pitch = event.dest_pitch;
    operation.resolve_width = event.resolve_width;
    operation.resolve_height = event.resolve_height;
    operation.succeeded = event.succeeded;
    operation.copy = event.copy;
    operation.clear_mode = event.clear_mode;
    operation.clear_flags = event.clear_flags;
    operation.rectangle_count = event.rectangle_count;
    operation.bounds = event.bounds;
    operation.clear_depth = event.clear_depth;
    operation.clear_color = event.clear_color;
    operations.push_back(operation);
  }
  return operations;
}

bool WithOrderedUiFrame(
    uint64_t source_frame,
    const std::function<bool(const std::map<uint64_t, OrderedUiDraw>&)>& use) {
  std::lock_guard lock(capture_mutex);
  const auto frame = captured_frames.find(source_frame);
  return frame != captured_frames.end() && CompleteUi(frame->second) &&
         use(frame->second.draws);
}

bool WithOrderedFrameDraws(
    uint64_t source_frame, uint64_t first_sequence, uint64_t last_sequence,
    const std::function<bool(const std::map<uint64_t, OrderedUiDraw>&)>& use) {
  if (!first_sequence || last_sequence < first_sequence ||
      last_sequence - first_sequence > 32) return false;
  std::lock_guard lock(capture_mutex);
  const auto found = captured_frames.find(source_frame);
  if (found == captured_frames.end() || found->second.rejected ||
      found->second.geometry_incomplete || found->second.state_incomplete)
    return false;
  const auto& frame = found->second;
  std::map<uint64_t, OrderedUiDraw> draws;
  for (auto it = frame.events.lower_bound(first_sequence);
       it != frame.events.end() && it->first <= last_sequence; ++it) {
    const auto& event = it->second;
    if (event.kind != 'D') continue;
    if (!event.state_snapshot_ready || event.final_seen != 1 ||
        event.constants.size() != 2048 || event.system.size() != 64 ||
        event.fetch.size() != 192 || event.bool_loop.size() != 40 ||
        event.texture_fetches != event.textures.size() ||
        std::any_of(event.textures.begin(), event.textures.end(),
                    [](const auto& texture) {
                      return !texture.allocation_id ||
                          !texture.payload_generation ||
                          texture.outdated_mask;
                    }))
      return false;
    OrderedUiDraw draw;
    draw.sequence = it->first;
    draw.vertex_shader = event.vertex_shader;
    draw.pixel_shader = event.pixel_shader;
    draw.vertex_specialization = event.vertex_specialization;
    draw.pixel_specialization = event.pixel_specialization;
    draw.primitive = event.primitive;
    draw.index_type = event.index_type;
    draw.index_count = event.index_count;
    draw.index_base = event.index_base;
    draw.index_length = event.index_length;
    draw.index_endianness = event.index_endianness;
    draw.surface = event.surface;
    draw.color = event.color;
    draw.depth = event.depth;
    draw.target_bits = event.target_bits;
    draw.depth_control = event.prepared_depth;
    draw.color_mask = event.color_mask;
    std::copy(event.constants.begin(), event.constants.end(),
              draw.constants.begin());
    draw.vertex_bitmap = event.vertex_bitmap;
    draw.pixel_bitmap = event.pixel_bitmap;
    if (draw.index_type) {
      const auto index = frame.geometry.find(
          {event.index_blob_hash, event.index_blob_length});
      if (event.index_snapshot_status != 1 ||
          event.index_blob_length != event.index_length ||
          index == frame.geometry.end()) return false;
      draw.index_hash = event.index_blob_hash;
      draw.indices = index->second;
    }
    for (const auto& input : event.vertices) {
      const auto vertex = frame.geometry.find(
          {input.blob_hash, input.blob_length});
      if (input.snapshot_status != 1 || !input.blob_length ||
          vertex == frame.geometry.end()) return false;
      OrderedUiVertex v;
      v.constant = input.fetch;
      v.stride = input.stride;
      v.base = input.base;
      v.length = input.length;
      v.type = input.type;
      v.hash = input.blob_hash;
      v.bytes = vertex->second;
      draw.vertices.push_back(std::move(v));
    }
    draw.final_seen = true;
    draw.raster = event.raster;
    draw.clip = event.clip;
    draw.final_depth = event.final_depth;
    std::copy(event.system.begin(), event.system.end(), draw.system.begin());
    std::copy(event.fetch.begin(), event.fetch.end(),
              draw.fetch_constants.begin());
    draw.viewport = event.viewport;
    draw.scissor = event.scissor;
    draw.textures = event.textures;
    draws.emplace(it->first, std::move(draw));
  }
  return !draws.empty() && use(draws);
}

uint64_t ResolveOrderedUiReplayFrame(uint64_t source_frame) {
  std::lock_guard lock(capture_mutex);
  const auto current = captured_frames.find(source_frame);
  if (current != captured_frames.end()) {
    if (CompleteUi(current->second)) return source_frame;
    if (current->second.rejected || !current->second.draws.empty()) return 0;
    if (std::any_of(current->second.events.begin(),
                    current->second.events.end(), [](const auto& entry) {
                      const auto& event = entry.second;
                      return event.surface == 0x14000500 &&
                             event.color == 0x000A0000;
                    })) return 0;
  }
  if (!source_frame) return 0;
  const auto previous = captured_frames.find(source_frame - 1);
  return previous != captured_frames.end() && CompleteUi(previous->second)
             ? source_frame - 1 : 0;
}

void CaptureOrderedFrameDraw(
    const rex::system::GraphicsPreparedDrawObservation& observation) {
  if (!observation.draw_sequence) return;
  Frame::Event event;
  event.surface = observation.surface_info;
  event.color = observation.color_info[0];
  event.depth = observation.depth_info;
  event.target_bits = observation.bound_render_target_bits;
  event.vertex_shader = observation.vertex_shader_hash;
  event.pixel_shader = observation.pixel_shader_hash;
  event.vertex_specialization = observation.vertex_specialization_mask;
  event.pixel_specialization = observation.pixel_specialization_mask;
  event.index_count = observation.index_count;
  event.primitive = observation.guest_primitive_type;
  event.index_type = observation.index_buffer_type;
  event.index_base = observation.index_buffer_guest_base;
  event.index_length = observation.index_buffer_length;
  event.index_endianness = observation.index_buffer_guest_endianness;
  event.index_snapshot_status = observation.index_cpu_snapshot_status;
  event.index_snapshot_hash = observation.index_cpu_snapshot_hash;
  if (observation.vertex_fetches)
    for (uint32_t i = 0;
         i < std::min(observation.vertex_fetch_count,
                      observation.vertex_fetch_capacity); ++i) {
      const auto& input = observation.vertex_fetches[i];
      event.vertices.push_back({input.fetch_constant, input.guest_base,
                                input.length, input.stride_words, input.type,
                                input.cpu_snapshot_status,
                                input.cpu_snapshot_length,
                                input.cpu_snapshot_hash});
    }
  event.prepared_depth = observation.normalized_depth_control;
  event.color_mask = observation.normalized_color_mask;
  event.vertex_fetches = observation.vertex_fetch_count;
  event.texture_fetches = observation.texture_fetch_count;
  std::lock_guard lock(capture_mutex);
  auto& frame = CaptureFrame(observation.frame_sequence);
  static const uint64_t trace_frame = std::strtoull(
      rex::cvar::GetFlagByName("pinyon_shift_snr01_trace_source_frame").c_str(),
      nullptr, 10);
  if (observation.frame_sequence == trace_frame) {
    event.state_bitmap_ready = observation.vertex_float_constant_bitmap &&
        (!event.pixel_shader || observation.pixel_float_constant_bitmap);
    if (event.state_bitmap_ready) {
      std::copy_n(observation.vertex_float_constant_bitmap, 4,
                  event.vertex_bitmap.begin());
      if (event.pixel_shader)
        std::copy_n(observation.pixel_float_constant_bitmap, 4,
                    event.pixel_bitmap.begin());
    }
    if (event.index_type && observation.index_cpu_snapshot_status == 1) {
      if (const auto hash = RecordGeometry(
              frame, observation.index_cpu_snapshot_bytes, event.index_length)) {
        event.index_blob_hash = *hash;
        event.index_blob_length = event.index_length;
      }
    }
    for (size_t i = 0; i < event.vertices.size(); ++i) {
      const auto& input = observation.vertex_fetches[i];
      if (input.cpu_snapshot_status != 1) continue;
      const uint32_t length = input.cpu_snapshot_length
          ? input.cpu_snapshot_length : input.length;
      if (length > input.length) {
        frame.geometry_incomplete = true;
        continue;
      }
      auto& vertex = event.vertices[i];
      if (const auto hash = RecordGeometry(
              frame, input.cpu_snapshot_bytes, length)) {
        vertex.blob_hash = *hash;
        vertex.blob_length = length;
      }
    }
  }
  if (frame.events.size() >= 8192 ||
      !frame.events.emplace(observation.draw_sequence, event).second)
    frame.rejected = true;
}

void CaptureOrderedFrameCopy(
    const rex::system::GraphicsCopyObservation& observation) {
  if (!observation.draw_sequence) return;
  Frame::Event event;
  event.kind = 'C';
  event.surface = observation.surface_info;
  event.color = observation.color_info[0];
  event.depth = observation.depth_info;
  event.dest_base = observation.rb_copy_dest_base;
  event.dest_pitch = observation.rb_copy_dest_pitch;
  event.resolve_width = observation.resolve_guest_width;
  event.resolve_height = observation.resolve_guest_height;
  event.succeeded = observation.succeeded;
  event.copy = {
      observation.rb_copy_control,
      observation.rb_copy_dest_info,
      observation.resolve_source_base_tiles,
      observation.resolve_source_pitch_tiles,
      observation.resolve_source_format,
      observation.resolve_source_guest_msaa_samples,
      observation.source_resource_width,
      observation.source_resource_height,
      observation.source_resource_format,
      observation.resolve_guest_offset_x,
      observation.resolve_guest_offset_y,
      observation.resolve_physical_offset_x,
      observation.resolve_physical_offset_y,
      observation.resolve_physical_width,
      observation.resolve_physical_height,
      observation.resolve_dest_offset_x,
      observation.resolve_dest_offset_y,
      observation.resolve_dest_pitch,
      observation.resolve_dest_height,
      observation.resolve_sample_select,
      uint32_t(observation.resolve_info_valid),
      uint32_t(observation.source_target_available)};
  std::lock_guard lock(capture_mutex);
  auto& frame = CaptureFrame(observation.frame_sequence);
  if (frame.events.size() >= 8192 ||
      !frame.events.emplace(observation.draw_sequence, event).second)
    frame.rejected = true;
}

void CaptureOrderedFrameClear(
    const rex::system::GraphicsFh1ClearObservation& observation) {
  if (!observation.draw_sequence || !observation.rectangle_count ||
      observation.rectangle_count > 2 || observation.mode > 2 ||
      !(observation.flags & 7)) {
    Reject(observation.frame_sequence);
    return;
  }
  std::lock_guard lock(capture_mutex);
  auto& frame = CaptureFrame(observation.frame_sequence);
  auto [it, inserted] = frame.events.try_emplace(observation.draw_sequence);
  auto& event = it->second;
  if ((!inserted && event.kind != 'D') || event.final_seen ||
      frame.events.size() > 8192) {
    frame.rejected = true;
    return;
  }
  event.kind = 'K';
  event.surface = observation.surface_info;
  event.color = observation.color_info;
  event.depth = observation.depth_info;
  event.clear_mode = observation.mode;
  event.clear_flags = observation.flags;
  event.stencil_reference = observation.stencil_reference;
  event.rectangle_count = observation.rectangle_count;
  event.succeeded = 1;
  for (uint32_t i = 0; i < observation.rectangle_count; ++i) {
    std::copy_n(observation.bounds[i], 4, event.bounds[i].begin());
    event.clear_depth[i] = observation.depth[i];
    std::copy_n(observation.colors[i], 4, event.clear_color[i].begin());
  }
}

void CaptureOrderedUiDraw(
    const rex::system::GraphicsPreparedDrawObservation& observation) {
  if (!observation.draw_sequence || !observation.vertex_float_constant_words ||
      !observation.vertex_float_constant_bitmap ||
      !observation.pixel_float_constant_bitmap ||
      observation.vertex_fetch_count > observation.vertex_fetch_capacity ||
      observation.vertex_fetch_count > 8 ||
      (observation.vertex_fetch_count && !observation.vertex_fetches) ||
      observation.texture_fetch_count > 32 ||
      (observation.texture_fetch_count && !observation.texture_fetches)) {
    Reject(observation.frame_sequence);
    return;
  }

  Draw draw;
  draw.sequence = observation.draw_sequence;
  draw.vertex_shader = observation.vertex_shader_hash;
  draw.pixel_shader = observation.pixel_shader_hash;
  draw.vertex_specialization = observation.vertex_specialization_mask;
  draw.pixel_specialization = observation.pixel_specialization_mask;
  draw.primitive = observation.guest_primitive_type;
  draw.index_type = observation.index_buffer_type;
  draw.index_count = observation.index_count;
  draw.index_base = observation.index_buffer_guest_base;
  draw.index_length = observation.index_buffer_length;
  draw.index_endianness = observation.index_buffer_guest_endianness;
  draw.surface = observation.surface_info;
  draw.color = observation.color_info[0];
  draw.depth = observation.depth_info;
  draw.target_bits = observation.bound_render_target_bits;
  draw.depth_control = observation.normalized_depth_control;
  draw.color_mask = observation.normalized_color_mask;
  std::copy_n(observation.vertex_float_constant_words, draw.constants.size(),
              draw.constants.begin());
  std::copy_n(observation.vertex_float_constant_bitmap, 4,
              draw.vertex_bitmap.begin());
  std::copy_n(observation.pixel_float_constant_bitmap, 4,
              draw.pixel_bitmap.begin());

  uint64_t payload_bytes = sizeof(draw.constants);
  for (uint32_t i = 0; i < observation.vertex_fetch_count; ++i) {
    const auto& input = observation.vertex_fetches[i];
    if (input.cpu_snapshot_status != 1 || !input.cpu_snapshot_bytes ||
        !input.cpu_snapshot_length ||
        input.cpu_snapshot_length > input.length) {
      Reject(observation.frame_sequence);
      return;
    }
    Vertex vertex;
    vertex.constant = input.fetch_constant;
    vertex.stride = input.stride_words;
    vertex.base = input.guest_base;
    vertex.length = input.length;
    vertex.type = input.type;
    vertex.bytes.assign(input.cpu_snapshot_bytes,
                        input.cpu_snapshot_bytes + input.cpu_snapshot_length);
    vertex.hash = ArtifactHash(vertex.bytes);
    payload_bytes += vertex.bytes.size();
    draw.vertices.push_back(std::move(vertex));
  }
  if (draw.index_type) {
    if (observation.index_cpu_snapshot_status != 1 ||
        !observation.index_cpu_snapshot_bytes) {
      Reject(observation.frame_sequence);
      return;
    }
    draw.indices.assign(observation.index_cpu_snapshot_bytes,
                        observation.index_cpu_snapshot_bytes + draw.index_length);
    draw.index_hash = ArtifactHash(draw.indices);
    payload_bytes += draw.indices.size();
  }
  if (observation.texture_fetch_count)
    draw.texture_fetches.assign(observation.texture_fetches,
                                observation.texture_fetches +
                                    observation.texture_fetch_count);

  std::lock_guard lock(capture_mutex);
  auto& frame = CaptureFrame(observation.frame_sequence);
  if (frame.draws.size() >= kMaximumDraws ||
      payload_bytes > kMaximumPayloadBytes -
                          std::min(frame.payload_bytes, kMaximumPayloadBytes) ||
      !frame.draws.emplace(draw.sequence, std::move(draw)).second) {
    frame.rejected = true;
    return;
  }
  frame.payload_bytes += payload_bytes;
}

void CaptureOrderedUiFinalState(
    const rex::system::GraphicsFinalDrawStateObservation& observation) {
  std::lock_guard lock(capture_mutex);
  const auto existing = captured_frames.find(observation.frame_sequence);
  if (existing == captured_frames.end()) return;
  auto& frame = existing->second;
  if (const auto event = frame.events.find(observation.draw_sequence);
      event != frame.events.end()) {
    event->second.final_seen++;
    if (observation.texture_count > 32 ||
        (observation.texture_count && !observation.textures) ||
        !observation.viewport || !observation.scissor)
      frame.rejected = true;
    else {
      event->second.raster = observation.raster_mode_control;
      event->second.clip = observation.clip_control;
      event->second.final_depth = observation.normalized_depth_control;
      std::copy_n(observation.viewport, 6, event->second.viewport.begin());
      std::copy_n(observation.scissor, 4, event->second.scissor.begin());
      if (observation.texture_count)
        event->second.textures.assign(
            observation.textures,
            observation.textures + observation.texture_count);
      for (uint32_t i = 0; i < observation.texture_count; ++i)
        event->second.versioned_textures +=
            observation.textures[i].allocation_id != 0 &&
            observation.textures[i].payload_generation != 0;
      static const uint64_t trace_frame = std::strtoull(
          rex::cvar::GetFlagByName(
              "pinyon_shift_snr01_trace_source_frame").c_str(), nullptr, 10);
      if (observation.frame_sequence == trace_frame) {
        auto& draw = event->second;
        draw.state_snapshot_ready = draw.state_bitmap_ready &&
            observation.vertex_float_constant_words &&
            observation.system_constant_words &&
            observation.system_constant_word_count >= 64 &&
            observation.fetch_constant_words &&
            observation.fetch_constant_word_count >= 192 &&
            observation.bool_loop_constant_words &&
            observation.bool_loop_constant_word_count >= 40;
        if (draw.state_snapshot_ready) {
          draw.dynamic_state = observation.dynamic_state;
          draw.constants.assign(observation.vertex_float_constant_words,
                                observation.vertex_float_constant_words + 2048);
          draw.system.assign(observation.system_constant_words,
                             observation.system_constant_words + 64);
          draw.fetch.assign(observation.fetch_constant_words,
                            observation.fetch_constant_words + 192);
          draw.bool_loop.assign(observation.bool_loop_constant_words,
                                observation.bool_loop_constant_words + 40);
        } else {
          frame.state_incomplete = true;
        }
      }
    }
  }
  const auto found = frame.draws.find(observation.draw_sequence);
  if (found == frame.draws.end()) return;
  auto& draw = found->second;
  if (draw.final_seen || !observation.system_constant_words ||
      observation.system_constant_word_count < draw.system.size() ||
      !observation.fetch_constant_words ||
      observation.fetch_constant_word_count < draw.fetch_constants.size() ||
      !observation.viewport || !observation.scissor ||
      observation.texture_count > 32 ||
      (observation.texture_count && !observation.textures)) {
    frame.rejected = true;
    return;
  }
  draw.final_seen = true;
  draw.raster = observation.raster_mode_control;
  draw.clip = observation.clip_control;
  draw.final_depth = observation.normalized_depth_control;
  std::copy_n(observation.system_constant_words, draw.system.size(),
              draw.system.begin());
  std::copy_n(observation.fetch_constant_words, draw.fetch_constants.size(),
              draw.fetch_constants.begin());
  std::copy_n(observation.viewport, draw.viewport.size(), draw.viewport.begin());
  std::copy_n(observation.scissor, draw.scissor.size(), draw.scissor.begin());
  if (observation.texture_count)
    draw.textures.assign(observation.textures,
                         observation.textures + observation.texture_count);
}

void FlushOrderedUiFrame(uint64_t output_frame) {
  static const uint64_t trace_frame = std::strtoull(
      rex::cvar::GetFlagByName("pinyon_shift_snr01_trace_source_frame").c_str(),
      nullptr, 10);
  static const uint64_t shadow_start = std::strtoull(
      rex::cvar::GetFlagByName("pinyon_shift_native_ui_shadow_start_frame").c_str(),
      nullptr, 10);
  const uint64_t source_frame = output_frame + 1;
  const uint64_t ui_replay_source_frame = source_frame == trace_frame
      ? ResolveOrderedUiReplayFrame(source_frame) : 0;
  const bool shadow = shadow_start && source_frame >= shadow_start &&
      source_frame - shadow_start < 24;
  if (!shadow && source_frame != trace_frame) return;
  Frame frame;
  Frame ui_frame;
  size_t draws = 0;
  uint64_t payload_bytes = 0;
  bool complete = false;
  {
    std::lock_guard lock(capture_mutex);
    if (const auto captured = captured_frames.find(source_frame);
        captured != captured_frames.end()) {
      draws = captured->second.draws.size();
      payload_bytes = captured->second.payload_bytes;
      complete = CompleteUi(captured->second);
      if (source_frame == trace_frame) frame = captured->second;
    }
    if (ui_replay_source_frame) {
      if (const auto retained = captured_frames.find(ui_replay_source_frame);
          retained != captured_frames.end() && CompleteUi(retained->second))
        ui_frame = retained->second;
    }
  }
  if (shadow)
    REXGPU_WARN("FH1 UI shadow source_frame={} draws={} complete={} bytes={}",
                source_frame, draws, complete, payload_bytes);
  if (source_frame != trace_frame || !frame.source_frame) return;
  const auto output = diagnostics::EnvironmentPath(
      "PINYON_SHIFT_FH1_RENDER_TEST_OUTPUT");
  if (!output) return;
  std::error_code error;
  std::filesystem::create_directories(*output, error);
  if (error) return;
  if (!frame.events.empty()) {
    const auto path = *output /
        ("ordered-frame-" + std::to_string(frame.source_frame) + ".csv");
    std::ofstream events(path, std::ios::trunc);
    if (events) {
      events << std::setprecision(9);
      events << "kind,sequence,surface,color,depth,target_bits,vertex_shader,"
                "pixel_shader,index_count,vertex_fetches,texture_fetches,"
                "final_seen,versioned_textures,dest_base,dest_pitch,"
                "resolve_width,resolve_height,succeeded,clear_mode,clear_flags,"
                "stencil_reference,rectangle_count,bounds0,depth0,color0,"
                "bounds1,depth1,color1,capture_rejected,ui_replay_source_frame,"
                "vertex_specialization,pixel_specialization,primitive,"
                "index_type,index_base,index_length,index_endianness,"
                "prepared_depth,color_mask,raster,clip,final_depth,"
                "viewport,scissor,texture_versions,index_snapshot_status,"
                "index_snapshot_hash,vertex_inputs,geometry_incomplete,"
                "index_blob_hash,index_blob_length,vertex_blobs,"
                "state_snapshot_ready,state_incomplete,copy_control,"
                "copy_dest_info,copy_source_base_tiles,"
                "copy_source_pitch_tiles,copy_source_format,copy_source_msaa,"
                "copy_source_resource_width,copy_source_resource_height,"
                "copy_source_resource_format,copy_source_x,copy_source_y,"
                "copy_physical_x,copy_physical_y,copy_physical_width,"
                "copy_physical_height,copy_dest_x,copy_dest_y,"
                "copy_dest_pitch,copy_dest_height,copy_sample_select,"
                "copy_info_valid,copy_source_available\n";
      for (const auto& [sequence, event] : frame.events) {
        events << event.kind << ',' << sequence << ',' << event.surface << ','
               << event.color << ',' << event.depth << ',' << event.target_bits
               << ',' << event.vertex_shader << ',' << event.pixel_shader << ','
               << event.index_count << ',' << event.vertex_fetches << ','
               << event.texture_fetches << ',' << event.final_seen << ','
               << event.versioned_textures << ',' << event.dest_base << ','
               << event.dest_pitch << ',' << event.resolve_width << ','
               << event.resolve_height << ',' << event.succeeded << ','
               << event.clear_mode << ',' << event.clear_flags << ','
               << event.stencil_reference << ',' << event.rectangle_count;
        for (size_t i = 0; i < 2; ++i) {
          events << ',';
          for (size_t j = 0; j < 4; ++j)
            events << (j ? ":" : "") << event.bounds[i][j];
          events << ',' << event.clear_depth[i] << ',';
          for (size_t j = 0; j < 4; ++j)
            events << (j ? ":" : "") << event.clear_color[i][j];
        }
        events << ',' << uint32_t(frame.rejected) << ','
               << ui_frame.source_frame << ','
               << event.vertex_specialization << ','
               << event.pixel_specialization << ',' << event.primitive << ','
               << event.index_type << ',' << event.index_base << ','
               << event.index_length << ',' << event.index_endianness << ','
               << event.prepared_depth << ',' << event.color_mask << ','
               << event.raster << ',' << event.clip << ','
               << event.final_depth << ',';
        for (size_t i = 0; i < event.viewport.size(); ++i)
          events << (i ? ":" : "") << event.viewport[i];
        events << ',';
        for (size_t i = 0; i < event.scissor.size(); ++i)
          events << (i ? ":" : "") << event.scissor[i];
        events << ',';
        for (size_t i = 0; i < event.textures.size(); ++i) {
          const auto& texture = event.textures[i];
          events << (i ? ";" : "") << texture.fetch_constant;
          for (uint32_t word : texture.fetch_words) events << ':' << word;
          events << ':' << texture.allocation_id << ':'
                 << texture.payload_generation << ':' << texture.outdated_mask;
        }
        events << ',' << event.index_snapshot_status << ','
               << event.index_snapshot_hash << ',';
        for (size_t i = 0; i < event.vertices.size(); ++i) {
          const auto& vertex = event.vertices[i];
          events << (i ? ";" : "") << vertex.fetch << ':' << vertex.base
                 << ':' << vertex.length << ':' << vertex.stride << ':'
                 << vertex.snapshot_status << ':' << vertex.snapshot_length
                 << ':' << vertex.snapshot_hash;
        }
        events << ',' << uint32_t(frame.geometry_incomplete) << ','
               << event.index_blob_hash << ',' << event.index_blob_length
               << ',';
        for (size_t i = 0; i < event.vertices.size(); ++i)
          events << (i ? ";" : "") << event.vertices[i].blob_hash << ':'
                 << event.vertices[i].blob_length;
        events << ',' << uint32_t(event.state_snapshot_ready) << ','
               << uint32_t(frame.state_incomplete) << ','
               << event.copy.control << ',' << event.copy.dest_info << ','
               << event.copy.source_base_tiles << ','
               << event.copy.source_pitch_tiles << ','
               << event.copy.source_format << ',' << event.copy.source_msaa
               << ',' << event.copy.source_resource_width << ','
               << event.copy.source_resource_height << ','
               << event.copy.source_resource_format << ','
               << event.copy.source_x << ',' << event.copy.source_y << ','
               << event.copy.physical_x << ',' << event.copy.physical_y << ','
               << event.copy.physical_width << ','
               << event.copy.physical_height << ',' << event.copy.dest_x
               << ',' << event.copy.dest_y << ',' << event.copy.dest_pitch
               << ',' << event.copy.dest_height << ','
               << event.copy.sample_select << ',' << event.copy.info_valid
               << ',' << event.copy.source_available;
        events << '\n';
      }
      events.close();
      REXGPU_INFO("FH1 RAY00 frame source={} events={} rejected={} "
                  "written={} path={}", frame.source_frame,
                  frame.events.size(), frame.rejected, bool(events),
                  path.string());
    }
  }
  if (!frame.events.empty()) {
    const auto path = *output /
        ("ordered-state-" + std::to_string(frame.source_frame) + ".bin");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (stream) {
      stream.write("RAYSTA01", 8);
      Write(stream, frame.source_frame);
      const uint32_t draw_count = uint32_t(std::count_if(
          frame.events.begin(), frame.events.end(),
          [](const auto& item) { return item.second.kind == 'D'; }));
      Write(stream, draw_count);
      for (const auto& [sequence, event] : frame.events) {
        if (event.kind != 'D') continue;
        Write(stream, sequence);
        Write(stream, uint32_t(event.state_snapshot_ready));
        if (!event.state_snapshot_ready) continue;
        Write(stream, event.dynamic_state);
        Write(stream, event.vertex_bitmap);
        Write(stream, event.pixel_bitmap);
        WriteWords(stream, event.constants);
        WriteWords(stream, event.system);
        WriteWords(stream, event.fetch);
        WriteWords(stream, event.bool_loop);
      }
      stream.close();
      REXGPU_INFO("FH1 RAY00 state frame={} draws={} incomplete={} "
                  "written={} path={}", frame.source_frame, draw_count,
                  frame.state_incomplete, bool(stream), path.string());
    }
  }
  if (!frame.geometry.empty()) {
    const auto path = *output /
        ("ordered-geometry-" + std::to_string(frame.source_frame) + ".bin");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (stream) {
      stream.write("RAYGEO01", 8);
      Write(stream, frame.source_frame);
      Write(stream, uint32_t(frame.geometry.size()));
      for (const auto& [key, bytes] : frame.geometry) {
        Write(stream, key.first);
        Write(stream, key.second);
        WriteBytes(stream, bytes);
      }
      stream.close();
      REXGPU_INFO("FH1 RAY00 geometry frame={} blobs={} bytes={} "
                  "incomplete={} written={} path={}", frame.source_frame,
                  frame.geometry.size(), frame.geometry_bytes,
                  frame.geometry_incomplete, bool(stream), path.string());
    }
  }
  if (ui_frame.draws.empty()) return;
  const auto path = *output /
      ("ordered-ui-" + std::to_string(ui_frame.source_frame) + ".bin");
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  if (!stream) return;
  stream.write("RAYUI002", 8);
  Write(stream, ui_frame.source_frame);
  Write(stream, uint32_t(ui_frame.draws.size()));
  Write(stream, uint32_t(CompleteUi(ui_frame)));
  for (const auto& [sequence, draw] : ui_frame.draws) {
    Write(stream, sequence);
    Write(stream, draw.vertex_shader);
    Write(stream, draw.pixel_shader);
    Write(stream, draw.vertex_specialization);
    Write(stream, draw.pixel_specialization);
    for (uint32_t value : {draw.primitive, draw.index_type, draw.index_count,
                           draw.index_base, draw.index_length,
                           draw.index_endianness, draw.surface, draw.color,
                           draw.depth, draw.target_bits, draw.depth_control,
                           draw.color_mask})
      Write(stream, value);
    Write(stream, draw.constants);
    Write(stream, draw.vertex_bitmap);
    Write(stream, draw.pixel_bitmap);
    Write(stream, uint32_t(draw.vertices.size()));
    for (const auto& vertex : draw.vertices) {
      for (uint32_t value : {vertex.constant, vertex.stride, vertex.base,
                             vertex.length, vertex.type,
                             uint32_t(vertex.bytes.size())})
        Write(stream, value);
      Write(stream, vertex.hash);
      WriteBytes(stream, vertex.bytes);
    }
    Write(stream, draw.index_hash);
    WriteBytes(stream, draw.indices);
    Write(stream, uint32_t(draw.texture_fetches.size()));
    for (const auto& texture : draw.texture_fetches)
      Write(stream, texture);
    Write(stream, uint32_t(draw.final_seen));
    Write(stream, draw.raster);
    Write(stream, draw.clip);
    Write(stream, draw.final_depth);
    Write(stream, draw.system);
    Write(stream, draw.fetch_constants);
    Write(stream, draw.viewport);
    Write(stream, draw.scissor);
    Write(stream, uint32_t(draw.textures.size()));
    for (const auto& texture : draw.textures)
      Write(stream, texture);
  }
  stream.close();
  REXGPU_INFO("FH1 RAY00 UI frame source={} draws={} payload_bytes={} "
              "complete={} written={} path={}", ui_frame.source_frame,
              ui_frame.draws.size(), ui_frame.payload_bytes,
              CompleteUi(ui_frame), bool(stream),
              path.string());
}

}  // namespace pinyon_shift::native_renderer
