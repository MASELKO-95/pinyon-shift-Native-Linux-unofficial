#pragma once

#if defined(__linux__)
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace pinyon_shift::save {
// /proc maps is a snapshot, not a lifetime guarantee. Read its ranges through
// /proc/self/mem (pread), never dereference them after inspecting permissions.
inline std::vector<std::pair<uintptr_t, uintptr_t>> ReadableMemoryRegions(
    uintptr_t begin, uintptr_t end) {
  std::vector<std::pair<uintptr_t, uintptr_t>> result;
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    unsigned long low = 0, high = 0;
    char permission = 0;
    if (std::sscanf(line.c_str(), "%lx-%lx %c", &low, &high, &permission) != 3 ||
        permission != 'r') continue;
    const uintptr_t first = std::max(begin, uintptr_t(low));
    const uintptr_t last = std::min(end, uintptr_t(high));
    if (first < last) result.emplace_back(first, last);
  }
  return result;
}
}  // namespace pinyon_shift::save
#endif
