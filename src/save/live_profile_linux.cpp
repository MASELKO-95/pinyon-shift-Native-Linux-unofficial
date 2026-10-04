#include "save/live_profile.h"
#include "save/linux_memory_regions.h"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <fcntl.h>
#include <unistd.h>

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
  const uint8_t* high = center + std::min<size_t>(window, (end - 4) - center);
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

std::vector<LiveCandidate> FindLinuxProfileValue(uintptr_t base, uint32_t value,
    uint32_t neighbour_a, uint32_t neighbour_b, uint32_t window, size_t max_candidates) {
  std::vector<LiveCandidate> candidates;
  if (!max_candidates || window > uint32_t(std::numeric_limits<int32_t>::max())) return candidates;
  const int memory = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
  if (memory < 0) return candidates;
  struct CloseFile { int fd; ~CloseFile() { close(fd); } } close_file{memory};
  // Copy readable snapshots in blocks with neighbour overlap. pread fails
  // safely if a guest mapping disappears while this on-demand scan runs.
  constexpr size_t kBlock = 1024 * 1024;
  for (const auto& [guest_begin, guest_end] : {
           std::pair<uintptr_t, uintptr_t>{0x40000000u, 0x80000000u},
           std::pair<uintptr_t, uintptr_t>{0xA0000000u, 0xC0000000u}}) {
    for (const auto& [region_begin, region_end] :
         ReadableMemoryRegions(base + guest_begin, base + guest_end)) {
      for (uintptr_t block = region_begin; block < region_end; block += kBlock) {
        const uintptr_t block_end = std::min(region_end, block + kBlock);
        const uintptr_t low = block - std::min<uintptr_t>(window, block - region_begin);
        const uintptr_t high = block_end + std::min<uintptr_t>(window, region_end - block_end);
        std::vector<uint8_t> snapshot(high - low);
        const ssize_t count = pread(memory, snapshot.data(), snapshot.size(), static_cast<off_t>(low));
        if (count < 4) continue;
        const uintptr_t read_end = low + size_t(count);
        uintptr_t address = block + ((4 - ((block - base) & 3)) & 3);
        for (; address + 4 <= std::min(block_end, read_end); address += 4) {
          const uint8_t* p = snapshot.data() + (address - low);
          if (LoadBe32(p) != value) continue;
          const auto a = window ? FindNear(snapshot.data(), snapshot.data() + count, p, neighbour_a, window)
                                : std::optional<int32_t>(0);
          const auto b = window ? FindNear(snapshot.data(), snapshot.data() + count, p, neighbour_b, window)
                                : std::optional<int32_t>(0);
          if (a && b) candidates.push_back({uint32_t(address - base), *a, *b});
          if (candidates.size() >= max_candidates) return candidates;
        }
      }
    }
  }
  return candidates;
}
}  // namespace pinyon_shift::save
