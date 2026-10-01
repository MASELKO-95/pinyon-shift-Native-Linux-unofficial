"""Check production range validation and page uploads against a byte-range oracle."""
import argparse
from pathlib import Path
import subprocess
import tempfile
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', default='clang++')
p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] / 'thirdparty/shiftglue-sdk/src/graphics/shared_memory.cpp')
a = p.parse_args()
s = a.source.read_text()
body = s[s.index('bool SharedMemory::RequestRanges('):s.index('bool SharedMemory::RequestRange(')]
code = r'''
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <random>
#include <span>
#include <utility>
#include <vector>
#define SCOPE_profile_cpu_f(...)
#define COUNT_profile_set(...)
namespace rex { bool bit_scan_forward(uint64_t x,uint32_t* out){if(!x)return false;*out=std::countr_zero(x);return true;} }
struct Region {std::mutex mutex;auto Acquire(){return std::unique_lock(mutex);}};
struct SharedMemory {
 static constexpr uint32_t kBufferSize=1024*1024;
 uint32_t page_size_log2_=12;
 Region global_critical_region_;
 std::vector<uint64_t> system_page_flags_valid_=std::vector<uint64_t>(4);
 std::vector<std::pair<uint32_t,uint32_t>> upload_ranges_,allocated,uploaded;
 bool allocation_ok=true,upload_ok=true;int upload_calls=0;
 bool EnsureHostGpuMemoryAllocated(uint32_t start,uint32_t len){allocated.emplace_back(start,len);return allocation_ok;}
 bool UploadRanges(const std::vector<std::pair<uint32_t,uint32_t>>& r){++upload_calls;uploaded=r;return upload_ok;}
 bool RequestRanges(const std::pair<uint32_t,uint32_t>*,size_t);
};
BODY
void check(std::vector<std::pair<uint32_t,uint32_t>> ranges, uint64_t seed, bool alloc=true, bool upload=true) {
 SharedMemory m;m.allocation_ok=alloc;m.upload_ok=upload;
 std::mt19937_64 random(seed);for(auto& bits:m.system_page_flags_valid_)bits=random();
 bool valid=true,nonempty=false;std::vector<bool> expected(256),actual(256);
 for(auto [start,len]:ranges){if(!len)continue;nonempty=true;if(start>m.kBufferSize || len>m.kBufferSize-start){valid=false;break;}
  for(uint32_t page=start>>12;page<=((start+len-1)>>12);++page)expected[page]=!(m.system_page_flags_valid_[page>>6]&(uint64_t(1)<<(page&63)));
 }
 bool ok=m.RequestRanges(ranges.data(),ranges.size());
 if(!valid){assert(!ok&&m.allocated.empty()&&m.uploaded.empty());return;}
 if(!alloc&&nonempty){assert(!ok&&m.uploaded.empty());return;}
 bool needs=std::find(expected.begin(),expected.end(),true)!=expected.end();assert(ok==(!needs||upload));assert(m.upload_calls==int(needs));
 for(auto [start,len]:m.uploaded){assert(start+len<=256);for(uint32_t page=start;page<start+len;++page){actual[page]=true;}}
 assert(actual==expected);
}
int main(){
 check({},1);check({{UINT32_MAX,0}},1);check({{1024*1024,1}},1);check({{UINT32_MAX,2}},1);
 check({{0,1024*1024}},1);check({{1024*1024-1,1}},1);
 check({{4095,2}},1);check({{0,4096},{2048,8192},{16384,4096}},1);
 check({{0,4096},{UINT32_MAX,2}},1);check({{0,8192}},1,false);check({{0,8192}},1,true,false);
 std::mt19937 random(42);
 for(int i=0;i<10000;++i){std::vector<std::pair<uint32_t,uint32_t>> ranges;
  size_t count=i%3?1:random()%12;for(size_t n=0;n<count;++n){uint32_t start=random()%(1024*1024+1),len=random()%65536;ranges.emplace_back(start,len);}
  check(ranges,i);
 }
 SharedMemory m;assert(m.RequestRanges(nullptr,5));
}
'''.replace('BODY', body)
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'check.cpp'; exe=Path(tmp)/'check.exe'; cpp.write_text(code)
    subprocess.run([a.compiler,'-std=c++20',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Shared ranges: validation, page oracle, boundaries and allocation/upload failures passed')
