"""Compile and exercise the production sampler-layout interning block.

Run in the release build environment, optionally passing --compiler clang++.
The extracted block keeps this check coupled to the production implementation.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='clang++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = (root / 'thirdparty/shiftglue-sdk/src/graphics/d3d12/pipeline_cache.cpp').read_text()
    marker = 'if (bindless_sampler_count) {\n        auto found_range'
    start = source.index(marker)
    depth = 0
    for end in range(source.index('{', start), len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            block = source[start:end + 1]
            break
    else:
        raise AssertionError('unterminated sampler-layout block')
    harness = r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
using std::size_t;
struct Binding { uint32_t bindless_descriptor_index; };
struct LayoutUID { size_t uid, vector_span_offset, vector_span_length; };
struct Cache {
  static constexpr size_t kLayoutUIDEmpty = 0;
  std::unordered_multimap<uint64_t, LayoutUID> bindless_sampler_layout_map_;
  std::vector<uint32_t> bindless_sampler_layouts_;
  size_t intern(std::vector<Binding> sampler_bindings, uint64_t bindless_sampler_layout_hash) {
    size_t bindless_sampler_count = sampler_bindings.size();
    size_t sampler_binding_count = sampler_bindings.size();
    size_t sampler_binding_layout_uid = kLayoutUIDEmpty;
    /* PRODUCTION */
    return sampler_binding_layout_uid;
  }
};
int main() {
  Cache cache;
  assert(cache.intern({}, 123) == 0);
  const auto first = cache.intern({{2}, {3}}, 123);
  assert(first == 1);
  assert(cache.intern({{2}, {3}}, 123) == first);
  // Same hash, distinct descriptor contents must remain distinct.
  const auto collision = cache.intern({{2}, {4}}, 123);
  assert(collision != first && collision != 0);
  // Same hash and prefix, different length must remain distinct too.
  const auto shorter = cache.intern({{2}}, 123);
  assert(shorter != first && shorter != collision && shorter != 0);
  for (int i = 0; i < 100; ++i) {
    assert(cache.intern({{2}, {3}}, 123) == first);
    assert(cache.intern({{2}, {4}}, 123) == collision);
    assert(cache.intern({{2}}, 123) == shorter);
  }
  assert(cache.bindless_sampler_layout_map_.size() == 3);
  assert(cache.bindless_sampler_layouts_.size() == 5);
}
'''.replace('/* PRODUCTION */', block)
    with tempfile.TemporaryDirectory(prefix='fh1-sampler-layout-') as directory:
        path = Path(directory)
        cpp, executable = path / 'check.cpp', path / 'check.exe'
        cpp.write_text(harness)
        subprocess.run([args.compiler, '-std=c++17', str(cpp), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
    print('Sampler layout reuse, nonzero IDs and hash collisions: passed')


if __name__ == '__main__':
    main()
