"""Execute the production CPU snapshot watch and lock protocol."""
import argparse
from pathlib import Path
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', default='clang++')
a = p.parse_args()
repo = Path(__file__).resolve().parents[1]
source = (repo/'thirdparty/shiftglue-sdk/src/graphics/shared_memory.cpp').read_text()
start = source.index('bool SharedMemory::CopyCpuSnapshot(')
method = source[start:source.index('void SharedMemory::RangeWrittenByGpu(', start)]
code = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <span>
struct SharedMemory {
 static constexpr uint32_t kBufferSize=512;
 using WatchHandle=void*;
 using Callback=void(*)(const int&,void*,void*,uint64_t,bool);
 struct Region {
  int held=0;
  struct Lock{Region& r;Lock(Region& r):r(r){++r.held;}~Lock(){--r.held;}};
  Lock Acquire(){return Lock(*this);}
 } global_critical_region_;
 Callback callback=nullptr;void* data=nullptr;
 bool active=false,allocation_ok=true,copy_ok=true;
 int invalidation=0,watched=0,copied=0,unwatched=0;
 WatchHandle WatchMemoryRange(uint32_t start,uint32_t size,Callback cb,void*,void* d,uint64_t){
  assert(global_critical_region_.held==1&&start==16&&size==8);++watched;
  if(!allocation_ok)return nullptr;
  callback=cb;data=d;active=true;return this;
 }
 void invalidate(){auto lock=global_critical_region_.Acquire();assert(active);active=false;callback(0,nullptr,data,0,true);}
 bool CopyCpuRange(uint32_t start,std::span<uint8_t> bytes){
  assert(!global_critical_region_.held&&start==16&&bytes.size()==8);++copied;
  if(invalidation==1)invalidate();
  if(copy_ok)for(auto& b:bytes)b=7;
  if(invalidation==2)invalidate();
  return copy_ok;
 }
 void UnwatchMemoryRange(WatchHandle h){assert(global_critical_region_.held==1&&h==this&&active);active=false;++unwatched;}
 bool CopyCpuSnapshot(uint32_t,std::span<uint8_t>);
};
METHOD
int main(){
 for(int invalidation:{0,1,2})for(bool copy_ok:{false,true}){
  SharedMemory m;m.invalidation=invalidation;m.copy_ok=copy_ok;std::array<uint8_t,8> bytes{};
  assert(m.CopyCpuSnapshot(16,bytes)==(copy_ok&&!invalidation));
  assert(m.watched==1&&m.copied==1&&m.unwatched==(!invalidation)&&!m.active&&!m.global_critical_region_.held);
 }
 {SharedMemory m;m.allocation_ok=false;std::array<uint8_t,8> b{};assert(!m.CopyCpuSnapshot(16,b));assert(!m.copied&&!m.unwatched);}
 {SharedMemory m;std::array<uint8_t,8> b{};assert(!m.CopyCpuSnapshot(505,b));assert(!m.CopyCpuSnapshot(0xffffffff,b));assert(!m.CopyCpuSnapshot(513,{}));assert(m.CopyCpuSnapshot(512,{}));assert(!m.watched&&!m.copied);}
}
'''.replace('METHOD', method)
with tempfile.TemporaryDirectory() as tmp:
    cpp, exe = Path(tmp)/'check.cpp', Path(tmp)/'check.exe'
    cpp.write_text(code)
    subprocess.run([a.compiler, '-std=c++20', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('CPU snapshot invalidation, rejection, cleanup and lock checks passed')
