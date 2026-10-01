"""Check production invalidation ranges, watches and GPU-history preservation."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', default='clang++')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
source = (repo / 'thirdparty/shiftglue-sdk/src/graphics/shared_memory.cpp').read_text()
method = source[source.index('std::pair<uint32_t, uint32_t> SharedMemory::MemoryInvalidationCallback('):source.index('bool SharedMemory::EnsureHostGpuMemoryAllocated(')]
harness = r'''
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <random>
#include <utility>
#include <vector>
bool fh1_narrow_cpu_invalidation;
#define REXCVAR_GET(name) name
namespace rex {
uint8_t lzcnt(uint64_t x) { return std::countl_zero(x); }
uint8_t tzcnt(uint64_t x) { return std::countr_zero(x); }
}
struct SharedMemory {
  static constexpr uint32_t kBufferSize = 1u << 29;
  uint32_t page_size_log2_;
  std::vector<uint64_t> system_page_flags_valid_, system_page_flags_valid_and_gpu_written_;
  struct Region { std::recursive_mutex mutex; auto Acquire() { return std::unique_lock(mutex); } } global_critical_region_;
  uint32_t calls = 0, watched_first = 0, watched_last = 0;
  void FireWatches(uint32_t first, uint32_t last, bool gpu) {
    assert(!gpu); ++calls; watched_first = first; watched_last = last;
  }
  std::pair<uint32_t, uint32_t> MemoryInvalidationCallback(uint32_t, uint32_t, bool);
};
''' + method + r'''
int main() {
  std::mt19937 random(624);
  for (uint32_t page_log : {12u, 14u, 16u}) for (bool narrow : {false, true}) {
    fh1_narrow_cpu_invalidation = narrow;
    SharedMemory s;
    s.page_size_log2_ = page_log;
    const uint32_t pages = s.kBufferSize >> page_log;
    s.system_page_flags_valid_.resize(pages / 64);
    s.system_page_flags_valid_and_gpu_written_.resize(pages / 64);
    for (uint32_t iteration = 0; iteration < 250; ++iteration) {
      const bool exact = iteration % 3 == 0;
      uint32_t address = random() % s.kBufferSize;
      uint32_t length = 1 + random() % (512 * 1024);
      if (iteration == 0) address = 0;
      if (iteration == 1) address = s.kBufferSize - 1;
      if (iteration == 2) { address = 65535; length = 2; }
      if (iteration == 3) { address = 262143; length = 2; }
      std::fill(s.system_page_flags_valid_.begin(), s.system_page_flags_valid_.end(), UINT64_MAX);
      for (auto& bits : s.system_page_flags_valid_and_gpu_written_) {
        bits = iteration % 2 ? 0 : (uint64_t(random()) << 32) | random();
      }
      const auto gpu_before = s.system_page_flags_valid_and_gpu_written_;
      const uint32_t first = address >> page_log;
      const uint32_t last = (address + std::min(length, s.kBufferSize - address) - 1) >> page_log;
      uint32_t expected_first = first, expected_last = last;
      auto gpu = [&](uint32_t page) { return (gpu_before[page / 64] >> (page % 64)) & 1; };
      if (!exact) {
        uint32_t lower = first & ~63u, upper = last | 63u;
        if (narrow) {
          uint32_t count = 65536u >> page_log;
          lower = std::max(lower, first / count * count);
          upper = std::min(upper, (last / count + 1) * count - 1);
        }
        while (expected_first > lower && !gpu(expected_first - 1)) --expected_first;
        while (expected_last < upper && !gpu(expected_last + 1)) ++expected_last;
      }
      s.calls = 0;
      auto result = s.MemoryInvalidationCallback(address, length, exact);
      assert(result.first == expected_first << page_log);
      assert(result.second == (expected_last - expected_first + 1) << page_log);
      assert(s.calls == 1 && s.watched_first == expected_first && s.watched_last == expected_last);
      for (uint32_t page = 0; page < pages; ++page) {
        bool invalid = page >= expected_first && page <= expected_last;
        assert(((s.system_page_flags_valid_[page / 64] >> (page % 64)) & 1) == !invalid);
        assert(((s.system_page_flags_valid_and_gpu_written_[page / 64] >> (page % 64)) & 1) == (!invalid && gpu(page)));
      }
    }
    const auto before = s.system_page_flags_valid_;
    auto calls = s.calls;
    for (auto request : {std::pair{0u, 0u}, std::pair{s.kBufferSize, 1u}, std::pair{UINT32_MAX, UINT32_MAX}}) {
      assert(s.MemoryInvalidationCallback(request.first, request.second, false) == std::make_pair(0u, UINT32_MAX));
      assert(s.calls == calls && s.system_page_flags_valid_ == before);
    }
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'check.cpp').write_text(harness)
    subprocess.run([args.compiler, '-std=c++20', '-O2', str(path / 'check.cpp'), '-o', str(path / 'check.exe')], check=True)
    subprocess.run([str(path / 'check.exe')], check=True)
print('PASS: 1,500 invalidation cases, exact/speculative modes, 4/16/64 KiB pages, GPU history, watch/returned ranges and bounds')
