#include "save/live_profile.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace pinyon_shift::save {
namespace {

uint32_t LoadBe32(const uint8_t* bytes) {
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) | (uint32_t(bytes[2]) << 8) |
         uint32_t(bytes[3]);
}

// Nearest aligned word equal to `value` within `window` of `center`, inside
// [begin, end).
std::optional<int32_t> FindNear(const uint8_t* begin, const uint8_t* end, const uint8_t* center,
                                uint32_t value, uint32_t window) {
  const uint8_t* low = std::max(begin, center - std::min<size_t>(window, center - begin));
  const uint8_t* high = std::min(end - 4, center + window);
  std::optional<int32_t> best;
  for (const uint8_t* p = low + ((center - low) & 3); p <= high; p += 4) {
    if (LoadBe32(p) == value) {
      const int32_t delta = static_cast<int32_t>(p - center);
      if (!best || std::abs(delta) < std::abs(*best)) best = delta;
    }
  }
  return best;
}

}  // namespace

std::vector<LiveCandidate> FindLiveProfileValue(uint32_t value, uint32_t neighbour_a,
                                                uint32_t neighbour_b, uint32_t window,
                                                size_t max_candidates) {
  std::vector<LiveCandidate> candidates;
#if defined(_WIN32)
  auto* kernel_state = rex::system::kernel_state();
  if (!kernel_state) return candidates;
  const auto* base = kernel_state->memory()->virtual_membase();
  // Title heaps: virtual 0x40000000-0x7FFFFFFF, and physical memory once
  // through its 0xA0000000 view (the 0xC0 and 0xE0 views alias it).
  static constexpr std::pair<uint32_t, uint32_t> kRanges[] = {{0x40000000u, 0x80000000u},
                                                              {0xA0000000u, 0xC0000000u}};
  for (const auto& [range_begin, range_end] : kRanges) {
    const uint8_t* cursor = base + range_begin;
    const uint8_t* const limit = base + range_end;
    while (cursor < limit && candidates.size() < max_candidates) {
      MEMORY_BASIC_INFORMATION info{};
      if (!VirtualQuery(cursor, &info, sizeof(info))) break;
      const auto* region = static_cast<const uint8_t*>(info.BaseAddress);
      const uint8_t* region_end = std::min(limit, region + info.RegionSize);
      const bool readable = info.State == MEM_COMMIT &&
                            (info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
                                             PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
                            !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS));
      if (readable) {
        const uint8_t* start = std::max(cursor, region);
        start += (4 - ((start - base) & 3)) & 3;
        for (const uint8_t* p = start; p + 4 <= region_end; p += 4) {
          if (LoadBe32(p) != value) continue;
          if (window == 0) {
            candidates.push_back({static_cast<uint32_t>(p - base), 0, 0});
            if (candidates.size() >= max_candidates) break;
            continue;
          }
          const auto a = FindNear(region, region_end, p, neighbour_a, window);
          if (!a) continue;
          const auto b = FindNear(region, region_end, p, neighbour_b, window);
          if (!b) continue;
          candidates.push_back({static_cast<uint32_t>(p - base), *a, *b});
          if (candidates.size() >= max_candidates) break;
        }
      }
      cursor = region_end > cursor ? region_end : cursor + 0x1000;
    }
  }
#else
  (void)value;
  (void)neighbour_a;
  (void)neighbour_b;
  (void)window;
  (void)max_candidates;
#endif
  return candidates;
}

}  // namespace pinyon_shift::save
