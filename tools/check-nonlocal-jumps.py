"""Check generated CRT call sites and exercise their real SDK helpers at -O2."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', default='clang++')
parser.add_argument('--generated', type=Path, default=Path('.local/generated/default'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
sdk = root / 'thirdparty/shiftglue-sdk'
generated = args.generated
pch = (generated / 'pinyon_shift_pch.h').read_text()
helpers = pch[pch.index('inline std::unordered_map<u32, jmp_buf>& get_jmp_buf_map()'):pch.index('// Trap Handling')]
sequence = re.compile(r'env = ctx;\s+temp\.s64 = ppc_setjmp\(ctx\.r3\.u32\);\s+if \(temp\.s64 != 0\) ctx = env;\s+ctx\.r3 = temp;')
setjmp_sites, longjmp_sites = [], []
for path in generated.glob('pinyon_shift_recomp.*.cpp'):
    source = path.read_text()
    assert not re.search(r'sub_(?:82A81E80|82A81950)\(ctx, base\)', source), path
    setjmp_sites += sequence.findall(source)
    longjmp_sites += re.findall(r'ppc_longjmp\(ctx\.r3\.u32, ctx\.r4\.s32\);', source)
    assert source.count('ppc_setjmp(') == len(sequence.findall(source)), path
assert len(setjmp_sites) == 9, len(setjmp_sites)
assert len(longjmp_sites) == 8, len(longjmp_sites)

harness = r'''
#include <rex/ppc/context.h>
#include <cassert>
#include <csetjmp>
#include <cstdlib>
#include <thread>
#include <unordered_map>
''' + helpers + r'''
void roundtrip(PPCContext& ctx, u32 key, int value, int depth, u32& guest_memory) {
  ctx.r3.u64 = key;
  const PPCContext original = ctx;
  PPCContext env{};
  PPCRegister temp{};
  ''' + setjmp_sites[0] + r'''
  if (ctx.r3.s64 == 0) {
    if (depth) roundtrip(ctx, key + 32, 17, depth - 1, guest_memory);
    ++guest_memory;
    ctx.r1.u64 = ctx.r31.u64 = ctx.lr = ctx.ctr.u64 = 0;
    ctx.f31.u64 = ctx.v127.u64[0] = ctx.v127.u64[1] = 0;
    ctx.cr7.set_raw(0);
    ctx.xer.ca = 0;
    ctx.r3.u64 = key;
    ctx.r4.s64 = value;
    ''' + longjmp_sites[0] + r'''
  }
  assert(ctx.r3.s64 == (value ? value : 1));
  assert(ctx.r1.u64 == original.r1.u64 && ctx.r31.u64 == original.r31.u64);
  assert(ctx.lr == original.lr && ctx.ctr.u64 == original.ctr.u64);
  assert(ctx.f31.u64 == original.f31.u64);
  assert(ctx.v127.u64[0] == original.v127.u64[0] && ctx.v127.u64[1] == original.v127.u64[1]);
  assert(ctx.cr7.raw() == original.cr7.raw() && ctx.xer.ca == original.xer.ca);
}
void check() {
  PPCContext ctx{};
  ctx.r1.u64 = 0x123456789ABCDEF0;
  ctx.r31.u64 = 0xFEDCBA9876543210;
  ctx.lr = 0x82001234;
  ctx.ctr.u64 = 0x42;
  ctx.f31.f64 = 1.25;
  ctx.v127.u64[0] = 0x1234;
  ctx.v127.u64[1] = 0xABCD;
  ctx.cr7.set_raw(9);
  ctx.xer.ca = 1;
  u32 guest_memory = 0;
  // Reuse the same guest key after its earlier host frame has returned.
  for (int value : {0, 1, -7, 123456}) roundtrip(ctx, 0x1000, value, 3, guest_memory);
  assert(guest_memory == 16);
  assert(get_jmp_buf_map().size() == 4);
}
int main() {
  check();
  std::thread other([] { assert(get_jmp_buf_map().empty()); check(); });
  other.join();
  assert(get_jmp_buf_map().size() == 4);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'check.cpp').write_text(harness)
    subprocess.run([args.compiler, '-std=c++23', '-O2', '-I', str(sdk / 'include'),
                    '-I', str(sdk / 'thirdparty/simde'), str(path / 'check.cpp'),
                    '-o', str(path / 'check.exe')], check=True)
    subprocess.run([str(path / 'check.exe')], check=True)
print('PASS: 9 setjmp and 8 longjmp call sites; optimized restoration, nesting, key reuse, memory effects and thread isolation')
