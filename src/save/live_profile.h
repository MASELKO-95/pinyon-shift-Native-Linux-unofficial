#pragma once

#include <cstdint>
#include <vector>

namespace pinyon_shift::save {

// Where the running title keeps a profile value (NP-8.3): guest words equal
// to `value` that have `neighbour_a` and `neighbour_b` (other fields of the
// same profile, big-endian 32-bit) within `window` bytes. Scans committed
// guest memory in the virtual and physical heaps; slow (tens of ms), so only
// call it on demand. A `window` of 0 lists every word equal to `value`.
struct LiveCandidate {
  uint32_t address = 0;
  int32_t neighbour_a_delta = 0;  // neighbour_a's address minus `address`
  int32_t neighbour_b_delta = 0;
};

std::vector<LiveCandidate> FindLiveProfileValue(uint32_t value, uint32_t neighbour_a,
                                                uint32_t neighbour_b, uint32_t window,
                                                size_t max_candidates = 32);

}  // namespace pinyon_shift::save
