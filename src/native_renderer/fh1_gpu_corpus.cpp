#include "native_renderer/fh1_gpu_corpus.h"

#include <atomic>
#include <chrono>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

#include <fmt/format.h>
#include <rex/cvar.h>

#include "native_renderer/fh1_pass_tracker.h"
#include "pinyon_shift_diagnostics.h"

REXCVAR_DEFINE_BOOL(pinyon_shift_fh1_gpu_corpus, false, "Pinyon Shift",
                    "Record the local FH1 V4 GPU execution corpus")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_INT32(pinyon_shift_fh1_corpus_checkpoint_seconds, 0, "Pinyon Shift",
                    "Periodic coverage checkpoint interval (0 = exit only)");

namespace pinyon_shift::native_renderer {
namespace {

constexpr size_t kMaximumCorpusKeys = 65536;
constexpr size_t kMaximumShaderFamilies = 4096;

struct ShaderFamilyEntry {
  uint64_t count = 0;
  uint64_t first_frame = 0;
  uint64_t last_frame = 0;
};

struct CorpusEntry {
  rex::system::GraphicsFh1ExecutionKey key;
  uint64_t vertex_shader = 0;
  uint64_t pixel_shader = 0;
  uint64_t count = 0;
  uint64_t first_frame = 0;
  uint64_t last_frame = 0;
  uint32_t index_count = 0;
  uint32_t index_buffer_guest_base = 0;
  uint32_t index_buffer_length = 0;
};

struct PassEntry {
  Fh1PassSummary summary;
  uint64_t occurrences = 0;
  uint64_t draw_executions = 0;
  uint64_t first_frame = 0;
  uint64_t last_frame = 0;
  bool first_draw_identity_varies = false;
};

std::atomic<bool> g_enabled{false};
std::mutex g_mutex;
std::map<uint64_t, CorpusEntry> g_entries;
std::map<std::pair<uint64_t, uint64_t>, ShaderFamilyEntry> g_shader_families;
uint64_t g_shader_family_overflow = 0;
std::map<uint64_t, PassEntry> g_passes;
Fh1PassTracker g_pass_tracker;
uint64_t g_overflow = 0;
uint64_t g_collisions = 0;
uint64_t g_pass_collisions = 0;
std::array<std::atomic<uint64_t>, 2> g_mode_counts{};
std::array<std::atomic<uint64_t>, 5> g_fallback_counts{};
std::atomic<uint64_t> g_runtime_shader_translations{0};
std::atomic<uint64_t> g_runtime_sync_pipeline_creations{0};

CorpusEntry* RecordKeyLocked(const rex::system::GraphicsFh1ExecutionKey& key,
                             uint64_t frame, uint64_t vertex_shader,
                             uint64_t pixel_shader, uint32_t index_count = 0,
                             uint32_t index_buffer_guest_base = 0,
                             uint32_t index_buffer_length = 0) {
  // Families remain discoverable after changing resource identities fill the
  // detailed inventory. Counts describe observed (possibly sampled) draws.
  if (key.kind == rex::system::GraphicsFh1ExecutionKind::kDraw) {
    const auto family_key = std::make_pair(vertex_shader, pixel_shader);
    auto family = g_shader_families.find(family_key);
    if (family != g_shader_families.end()) {
      ++family->second.count;
      family->second.last_frame = frame;
    } else if (g_shader_families.size() < kMaximumShaderFamilies) {
      g_shader_families.emplace(family_key, ShaderFamilyEntry{1, frame, frame});
    } else {
      ++g_shader_family_overflow;
    }
  }
  auto found = g_entries.find(key.identity);
  if (found != g_entries.end()) {
    if (found->second.key != key) {
      ++g_collisions;
      return nullptr;
    }
    ++found->second.count;
    found->second.last_frame = frame;
    return &found->second;
  }
  if (g_entries.size() == kMaximumCorpusKeys) {
    ++g_overflow;
    return nullptr;
  }
  return &g_entries
              .emplace(key.identity,
                       CorpusEntry{key, vertex_shader, pixel_shader, 1, frame,
                                   frame, index_count,
                                   index_buffer_guest_base,
                                   index_buffer_length})
              .first->second;
}

void RecordPassLocked(const Fh1PassSummary& summary) {
  if (g_passes.size() >= kMaximumCorpusKeys &&
      !g_passes.contains(summary.signature)) {
    ++g_overflow;
    return;
  }
  auto [found, inserted] = g_passes.try_emplace(
      summary.signature,
      PassEntry{summary, 1, summary.draw_count, summary.frame,
                summary.frame});
  if (inserted) {
    return;
  }
  if (found->second.summary.attachment_state != summary.attachment_state ||
      found->second.summary.first_draw_family != summary.first_draw_family ||
      found->second.summary.terminal_copy_state !=
          summary.terminal_copy_state ||
      found->second.summary.draw_count != summary.draw_count) {
    ++g_pass_collisions;
    return;
  }
  ++found->second.occurrences;
  // A pass signature groups draw families, not changing buffer identities.
  // Keep the first identity as a representative and make variation explicit.
  found->second.first_draw_identity_varies |=
      found->second.summary.first_draw_identity != summary.first_draw_identity;
  found->second.draw_executions += summary.draw_count;
  found->second.summary.prepare_cpu_time_ns += summary.prepare_cpu_time_ns;
  found->second.last_frame = summary.frame;
  found->second.summary.hazard_flags |= summary.hazard_flags;
}

}  // namespace

bool ResetFh1GpuCorpus() {
  std::lock_guard lock(g_mutex);
  g_entries.clear();
  g_shader_families.clear();
  g_shader_family_overflow = 0;
  g_passes.clear();
  g_pass_tracker = {};
  g_overflow = 0;
  g_collisions = 0;
  g_pass_collisions = 0;
  for (auto& count : g_mode_counts) {
    count.store(0, std::memory_order_relaxed);
  }
  for (auto& count : g_fallback_counts) {
    count.store(0, std::memory_order_relaxed);
  }
  g_runtime_shader_translations.store(0, std::memory_order_relaxed);
  g_runtime_sync_pipeline_creations.store(0, std::memory_order_relaxed);
  const bool enabled = REXCVAR_GET(pinyon_shift_fh1_gpu_corpus);
  g_enabled.store(enabled, std::memory_order_release);
  return enabled;
}

void RecordFh1GpuExecution(
    const rex::system::GraphicsFh1ExecutionKey& key, uint64_t frame,
    uint64_t vertex_shader, uint64_t pixel_shader) {
  if (!g_enabled.load(std::memory_order_acquire) ||
      key.version != rex::system::GraphicsFh1ExecutionKey::kVersion ||
      !key.identity || key.identity != key.ComputeIdentity()) {
    return;
  }
  std::lock_guard lock(g_mutex);
  RecordKeyLocked(key, frame, vertex_shader, pixel_shader);
}

void FlushFh1GpuCorpus(bool final) {
  if (!(final ? g_enabled.exchange(false, std::memory_order_acq_rel)
              : g_enabled.load(std::memory_order_acquire))) {
    return;
  }
  diagnostics::RecordEvent(
      "native_renderer.v4.execution.summary",
      {{"compatibility",
        fmt::format("{}", g_mode_counts[0].load(std::memory_order_relaxed))},
       {"covered_in_place",
        fmt::format("{}", g_mode_counts[1].load(std::memory_order_relaxed))},
       {"manifest_unavailable",
        fmt::format("{}", g_fallback_counts[1].load(std::memory_order_relaxed))},
       {"key_not_captured",
        fmt::format("{}", g_fallback_counts[2].load(std::memory_order_relaxed))},
       {"hazardous_draw",
        fmt::format("{}", g_fallback_counts[3].load(std::memory_order_relaxed))},
       {"pipeline_not_prewarmed",
        fmt::format("{}", g_fallback_counts[4].load(std::memory_order_relaxed))},
       {"route_runtime_shader_translations",
        fmt::format("{}",
                    g_runtime_shader_translations.load(
                        std::memory_order_relaxed))},
       {"route_runtime_sync_pipeline_creations",
        fmt::format("{}",
                    g_runtime_sync_pipeline_creations.load(
                        std::memory_order_relaxed))}});
  std::lock_guard lock(g_mutex);
  if (final) {
    if (const auto pass = g_pass_tracker.Finish()) {
      RecordPassLocked(*pass);
    }
  }
  const auto root = diagnostics::StateRoot() / "cache" / "fh1-gpu-corpus";
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) {
    diagnostics::RecordEvent("native_renderer.v4.corpus.write",
                             {{"status", "create_directory_failed"}});
    return;
  }
  const auto output = root / (diagnostics::SessionId() + ".json");
  const auto staging = output.string() + ".tmp";
  std::ofstream stream(staging, std::ios::binary | std::ios::trunc);
  if (!stream) {
    diagnostics::RecordEvent("native_renderer.v4.corpus.write",
                             {{"status", "open_failed"}});
    return;
  }
  stream << "{\n  \"schema\": \"pinyon-shift.fh1-gpu-corpus.v3\",\n"
         << "  \"observation_frame_stride\": "
         << 1
         << ",\n"
         << "  \"key_version\": "
         << rex::system::GraphicsFh1ExecutionKey::kVersion << ",\n"
         << "  \"unique_keys\": " << g_entries.size() << ",\n"
         << "  \"unique_passes\": " << g_passes.size() << ",\n"
         << "  \"overflow\": " << g_overflow << ",\n"
         << "  \"collisions\": " << g_collisions << ",\n"
         << "  \"pass_collisions\": " << g_pass_collisions << ",\n"
         << "  \"shader_family_overflow\": " << g_shader_family_overflow << ",\n"
         << "  \"shader_families\": [\n";
  bool first_family = true;
  for (const auto& [shaders, family] : g_shader_families) {
    if (!first_family) stream << ",\n";
    first_family = false;
    stream << fmt::format(
        "    {{\"vertex_shader\":\"{:016X}\",\"pixel_shader\":\"{:016X}\","
        "\"count\":{},\"first_frame\":{},\"last_frame\":{}}}",
        shaders.first, shaders.second, family.count, family.first_frame,
        family.last_frame);
  }
  stream << "\n  ],\n  \"entries\": [\n";
  bool first = true;
  for (const auto& [identity, entry] : g_entries) {
    if (!first) {
      stream << ",\n";
    }
    first = false;
    stream << fmt::format(
        "    {{\"identity\":\"{:016X}\",\"kind\":{},"
        "\"count\":{},\"first_frame\":{},\"last_frame\":{},"
        "\"index_count\":{},\"index_buffer_guest_base\":{},"
        "\"index_buffer_length\":{},"
        "\"vertex_shader\":\"{:016X}\",\"pixel_shader\":\"{:016X}\","
        "\"shader_state\":\"{:016X}\",\"pipeline_state\":\"{:016X}\","
        "\"attachment_state\":\"{:016X}\",\"resource_state\":\"{:016X}\","
        "\"dynamic_state\":\"{:016X}\",\"operation_state\":\"{:016X}\","
        "\"hazard_flags\":{},\"flags\":{}",
        identity, uint32_t(entry.key.kind), entry.count, entry.first_frame,
        entry.last_frame, entry.index_count, entry.index_buffer_guest_base,
        entry.index_buffer_length, entry.vertex_shader, entry.pixel_shader,
        entry.key.shader_state, entry.key.pipeline_state,
        entry.key.attachment_state, entry.key.resource_state,
        entry.key.dynamic_state, entry.key.operation_state,
        entry.key.hazard_flags, entry.key.flags);
    stream << "}";
  }
  stream << "\n  ],\n  \"passes\": [\n";
  first = true;
  for (const auto& [signature, entry] : g_passes) {
    if (!first) {
      stream << ",\n";
    }
    first = false;
    stream << fmt::format(
        "    {{\"signature\":\"{:016X}\"," 
        "\"attachment_state\":\"{:016X}\"," 
        "\"first_draw_family\":\"{:016X}\"," 
        "\"first_draw_identity\":\"{:016X}\"," 
        "\"first_draw_identity_varies\":{},"
        "\"terminal_copy_state\":\"{:016X}\"," 
        "\"draw_count\":{},\"hazard_flags\":{},"
        "\"prepare_cpu_time_ns\":{},"
        "\"average_prepare_cpu_time_ns\":{},"
        "\"average_prepare_cpu_time_per_draw_ns\":{},"
        "\"occurrences\":{},\"draw_executions\":{},"
        "\"first_frame\":{},\"last_frame\":{}}}",
        signature, entry.summary.attachment_state,
        entry.summary.first_draw_family,
        entry.summary.first_draw_identity,
        entry.first_draw_identity_varies,
        entry.summary.terminal_copy_state, entry.summary.draw_count,
        entry.summary.hazard_flags, entry.summary.prepare_cpu_time_ns,
        entry.occurrences
            ? entry.summary.prepare_cpu_time_ns / entry.occurrences
            : 0,
        entry.draw_executions
            ? entry.summary.prepare_cpu_time_ns / entry.draw_executions
            : 0,
        entry.occurrences, entry.draw_executions, entry.first_frame,
        entry.last_frame);
  }
  stream << "\n  ]\n}\n";
  stream.close();
  if (!stream) {
    std::filesystem::remove(staging, error);
    diagnostics::RecordEvent("native_renderer.v4.corpus.write",
                             {{"status", "write_failed"}});
    return;
  }
  std::filesystem::rename(staging, output, error);
  diagnostics::RecordEvent(
      "native_renderer.v4.corpus.write",
      {{"status", error ? "commit_failed" : "written"},
       {"final", final ? "1" : "0"},
       {"path", output.string()},
       {"unique_keys", fmt::format("{}", g_entries.size())},
       {"unique_passes", fmt::format("{}", g_passes.size())},
       {"overflow", fmt::format("{}", g_overflow)},
       {"collisions", fmt::format("{}", g_collisions)},
       {"pass_collisions", fmt::format("{}", g_pass_collisions)}});
}

}  // namespace pinyon_shift::native_renderer
