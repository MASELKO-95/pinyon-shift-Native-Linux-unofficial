"""Compile the production CPU-source guard and check page/word boundaries."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='clang++')
    args=parser.parse_args()
    source=(Path(__file__).resolve().parents[1]/'thirdparty/shiftglue-sdk/src/graphics/shared_memory.cpp').read_text()
    method=source[source.index('bool SharedMemory::CopyCpuRange('):source.index('bool SharedMemory::CopyCpuSnapshot(')]
    harness=r"""
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <span>
#include <vector>
struct SharedMemory {
  static constexpr uint32_t kBufferSize=1u<<29;
  uint32_t page_size_log2_=12;
  bool memory_invalidation_callback_handle_=true;
  std::vector<uint64_t> system_page_flags_valid_and_gpu_written_=std::vector<uint64_t>(2048);
  struct Region {std::recursive_mutex mutex;auto Acquire(){return std::unique_lock(mutex);}} global_critical_region_;
  struct Memory {
    std::vector<uint8_t> bytes=std::vector<uint8_t>(1<<20);
    uint32_t protected_start=0,protected_size=0;
    void EnablePhysicalMemoryAccessCallbacks(uint32_t start,uint32_t size,bool invalidation,bool provider){
      assert(invalidation&&!provider);protected_start=start;protected_size=size;
    }
    uint8_t* TranslatePhysical(uint32_t address){
      assert(protected_size && address>=protected_start && address-protected_start<protected_size);
      return bytes.data()+(address&((1<<20)-1));
    }
  } ram;
  Memory& memory(){return ram;}
  bool CopyCpuRange(uint32_t,std::span<uint8_t>);
};
METHOD
int main(){
  SharedMemory s;
  for(size_t i=0;i<s.ram.bytes.size();++i)s.ram.bytes[i]=uint8_t(i);
  std::vector<uint8_t> result(8192,0xAA);
  assert(s.CopyCpuRange(63*4096+13,result));
  assert(s.ram.protected_start==63*4096+13 && s.ram.protected_size==result.size());
  assert(!std::memcmp(result.data(),s.ram.TranslatePhysical(63*4096+13),result.size()));
  for(uint32_t page:{63,64,65}) {
    s.system_page_flags_valid_and_gpu_written_[page>>6] |= uint64_t(1)<<(page&63);
    result.assign(result.size(),0xAA);
    assert(!s.CopyCpuRange(63*4096+13,result));
    for(auto byte:result)assert(byte==0xAA);
    s.system_page_flags_valid_and_gpu_written_[page>>6]=0;
  }
  s.system_page_flags_valid_and_gpu_written_[0]=uint64_t(1)<<62;
  s.system_page_flags_valid_and_gpu_written_[1]=uint64_t(1)<<1;
  assert(s.CopyCpuRange(63*4096,std::span(result).first(8192)));
  // One byte into the next GPU-owned page is enough to reject the entire copy.
  result.resize(8193);assert(!s.CopyCpuRange(63*4096,result));
  assert(s.CopyCpuRange(SharedMemory::kBufferSize,{}));
  assert(!s.CopyCpuRange(SharedMemory::kBufferSize+1,{}));
  assert(!s.CopyCpuRange(SharedMemory::kBufferSize,std::span(result).first(1)));
  assert(!s.CopyCpuRange(UINT32_MAX,result));
  result.resize(1);assert(s.CopyCpuRange(SharedMemory::kBufferSize-1,result));
  s.system_page_flags_valid_and_gpu_written_.back()=uint64_t(1)<<63;
  assert(!s.CopyCpuRange(SharedMemory::kBufferSize-1,result));
  s.memory_invalidation_callback_handle_=false;
  result.assign(1,0xAA);assert(!s.CopyCpuRange(0,result));assert(result[0]==0xAA);
  s.memory_invalidation_callback_handle_=true;
  s.system_page_flags_valid_and_gpu_written_.clear();
  assert(!s.CopyCpuRange(0,result));
}
""".replace('METHOD',method)
    with tempfile.TemporaryDirectory() as temp:
        cpp=Path(temp)/'source.cpp';exe=Path(temp)/'source.exe'
        cpp.write_text(harness)
        subprocess.run([args.compiler,'-std=c++20',str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('CPU source ownership, extent and unchanged-output checks passed')


if __name__=='__main__':main()
