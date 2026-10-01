"""Compile the production texture load lifecycle with injected memory writes.

Use --source with a prior cache.cpp to reproduce a lost-write regression.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default='clang++')
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1] /
                        'thirdparty/shiftglue-sdk/src/graphics/pipeline/texture/cache.cpp')
    args = parser.parse_args()
    source = args.source.read_text()
    signatures = ['bool TextureCache::PrepareTextureLoad(', 'bool TextureCache::CommitPreparedTextureLoad(',
                  'bool TextureCache::LoadTextureData(', 'void TextureCache::Texture::WatchCallback(',
                  'void TextureCache::WatchCallback(']
    signatures += (['void TextureCache::Texture::MakeUpToDateAndWatch(']
                   if 'void TextureCache::Texture::MakeUpToDateAndWatch(' in source else
                   ['void TextureCache::Texture::WatchPendingLoad(', 'void TextureCache::Texture::CompleteLoad('])
    methods = []
    for signature in signatures:
        start = source.index(signature)
        depth = 0
        for end in range(source.index('{', start), len(source)):
            depth += (source[end] == '{') - (source[end] == '}')
            if depth == 0:
                methods.append(source[start:end+1])
                break
    harness = r"""
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <iostream>
#include <utility>
#undef assert
#define assert(x) ((x) ? void(0) : throw std::runtime_error(#x))
#define assert_not_zero(x) assert(x)
constexpr int kTextureDirtyLoadAttempts=0;
int dirty_load_attempts=0;
void PERF_counter_inc(int id){assert(id==kTextureDirtyLoadAttempts);++dirty_load_attempts;}
namespace rex { template<class T> T align(T a,T b) { return (a+b-1)&~(b-1); } }
struct Region { std::recursive_mutex mutex; auto Acquire() { return std::unique_lock(mutex); } };
struct SharedMemory {
  using WatchHandle=void*;
  using Callback=void(*)(const std::unique_lock<std::recursive_mutex>&,void*,void*,uint64_t,bool);
  struct Watch { Callback callback; void* context; void* data; uint64_t arg; };
  Region region; uintptr_t next=1; std::map<void*,Watch> watches;
  int request_write=-1, requests=0; bool gpu=false, fail=false;
  void* WatchMemoryRange(uint32_t,uint32_t,Callback cb,void* ctx,void* data,uint64_t arg) {
    auto handle=reinterpret_cast<void*>(next++); watches.emplace(handle,Watch{cb,ctx,data,arg}); return handle;
  }
  void fire(int part) {
    auto lock=region.Acquire();
    for(auto it=watches.begin();it!=watches.end();) {
      if(it->second.arg!=uint64_t(part)) {++it;continue;}
      auto w=it->second; it=watches.erase(it); w.callback(lock,w.context,w.data,w.arg,gpu);
    }
  }
  bool RequestRanges(const std::pair<uint32_t,uint32_t>*,size_t) {
    ++requests;
    if(request_write>=0) fire(std::exchange(request_write,-1));
    return !fail;
  }
};
struct TextureCache {
  struct TextureKey { bool scaled_resolve=false; uint32_t base_page=1,mip_page=2; };
  struct Texture {
    TextureCache& cache;
    TextureKey k;
    bool base_outdated_=true,mips_outdated_=true;
    std::atomic<uint32_t> outdated_mask_{3};
    SharedMemory::WatchHandle base_watch_handle_=nullptr,mips_watch_handle_=nullptr;
    static constexpr uint32_t kOutdatedBitBase=1,kOutdatedBitMips=2;
    TextureCache& texture_cache() {return cache;}
    TextureKey key() {return k;}
    uint32_t GetGuestBaseSize() const {return 64;}
    uint32_t GetGuestMipsSize() const {return 64;}
    uint32_t outdated_mask() const {return outdated_mask_.load();}
    bool base_outdated(const std::unique_lock<std::recursive_mutex>&) const {return base_outdated_;}
    bool mips_outdated(const std::unique_lock<std::recursive_mutex>&) const {return mips_outdated_;}
    void LogAction(const char*) {}
    void MakeUpToDateAndWatch(const std::unique_lock<std::recursive_mutex>&);
    void WatchPendingLoad(const std::unique_lock<std::recursive_mutex>&,bool,bool);
    void CompleteLoad(const std::unique_lock<std::recursive_mutex>&,bool,bool);
    void WatchCallback(const std::unique_lock<std::recursive_mutex>&,bool);
  };
  struct PendingTextureLoad {Texture* texture=nullptr; bool load_base=false,load_mips=false;};
  struct PendingSharedMemoryRange {uint32_t start,length;};
  SharedMemory memory; Region global_critical_region_;
  std::atomic<bool> texture_became_outdated_{false};
  bool cpu_import=false; int cpu_write=-1;
  bool TryLoadTextureDataFromCpu(Texture&,bool,bool) {
    assert(memory.watches.size()==2);
    if(cpu_write>=0) memory.fire(std::exchange(cpu_write,-1));
    return cpu_import;
  }
  int backend_calls=0;
  int backend_write=-1; bool backend_fail=false,scaled_fail=false;
  SharedMemory& shared_memory() {return memory;}
  bool EnsureScaledResolveMemoryCommitted(uint32_t,uint32_t,int) {return !scaled_fail;}
  bool LoadTextureDataFromResidentMemoryImpl(Texture&,bool,bool) {
    ++backend_calls;
    if(backend_write>=0) memory.fire(std::exchange(backend_write,-1));
    return !backend_fail;
  }
  bool PrepareTextureLoad(Texture&,PendingTextureLoad&,PendingSharedMemoryRange*,size_t&);
  bool CommitPreparedTextureLoad(const PendingTextureLoad&);
  bool LoadTextureData(Texture&);
  static void WatchCallback(const std::unique_lock<std::recursive_mutex>&,void*,void*,uint64_t,bool);
};
METHODS
int main() try {
  { TextureCache c; TextureCache::Texture t{c}; dirty_load_attempts=0;
    assert(c.LoadTextureData(t)); assert(dirty_load_attempts==1);
    TextureCache::PendingTextureLoad p; TextureCache::PendingSharedMemoryRange ranges[2]; size_t n=0;
    assert(!c.PrepareTextureLoad(t,p,ranges,n)); assert(dirty_load_attempts==1); }

  // CPU success skips residency; writes survive completion and failed attempts fall back.
  for(bool success:{false,true}) for(int part:{-1,0,1}) {
    TextureCache c; TextureCache::Texture t{c}; c.cpu_import=success;c.cpu_write=part;
    assert(c.LoadTextureData(t));assert(c.memory.requests==(success?0:1));
    assert(c.backend_calls==(success?0:1));
    assert(t.outdated_mask()==(part<0?0u:1u<<part));
    if(part>=0) {assert(c.LoadTextureData(t));assert(!t.outdated_mask());}
  }
  // Partial CPU imports must preserve a write to the unrequested sibling.
  for(int part:{0,1}) {
    TextureCache c;TextureCache::Texture t{c};c.cpu_import=true;
    assert(c.LoadTextureData(t));c.memory.fire(part);c.cpu_write=1-part;
    assert(c.LoadTextureData(t));assert(t.outdated_mask()==(1u<<(1-part)));
    assert(c.memory.requests==0);
  }
  // Baseline: successful loads become clean and preserve exactly two watches.
  { TextureCache c; TextureCache::Texture t{c}; assert(c.LoadTextureData(t));
    assert(!t.outdated_mask()); assert(c.memory.watches.size()==2); }
  // Each writer/source stage must leave its region dirty after loading.
  for(bool gpu:{false,true}) for(bool backend:{false,true}) for(int part:{0,1}) {
    TextureCache c; TextureCache::Texture t{c}; c.memory.gpu=gpu;
    (backend?c.backend_write:c.memory.request_write)=part;
    assert(c.LoadTextureData(t));
    assert(t.outdated_mask()==(1u<<part));
    assert(c.texture_became_outdated_.load());
    assert(c.LoadTextureData(t)); assert(!t.outdated_mask());
    assert(c.memory.watches.size()==2);
  }
  // A base write must survive a mips-only load; no clearing unrequested ranges.
  { TextureCache c; TextureCache::Texture t{c}; assert(c.LoadTextureData(t));
    c.memory.fire(1); c.backend_write=0; assert(c.LoadTextureData(t));
    assert(t.outdated_mask()==1); }
  for(int failure:{0,1,2}) {
    TextureCache c; TextureCache::Texture t{c};
    c.memory.fail=failure==0; c.backend_fail=failure==1;
    t.k.scaled_resolve=c.scaled_fail=failure==2;
    assert(!c.LoadTextureData(t)); assert(t.outdated_mask()==3);
    assert(c.texture_became_outdated_.load());
    c.memory.fail=c.backend_fail=c.scaled_fail=false;
    assert(c.LoadTextureData(t)); assert(!t.outdated_mask());
    assert(c.memory.watches.size()==2);
  }
  // A failed batch can prepare again without leaking or replacing valid watches.
  { TextureCache c; TextureCache::Texture t{c};
    TextureCache::PendingTextureLoad p; TextureCache::PendingSharedMemoryRange ranges[2]; size_t n=0;
    assert(c.PrepareTextureLoad(t,p,ranges,n)); auto handles=c.memory.watches.size();
    assert(c.PrepareTextureLoad(t,p,ranges,n)); assert(c.memory.watches.size()==handles);
    assert(c.CommitPreparedTextureLoad(p)); assert(!t.outdated_mask()); }
} catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
""".replace('METHODS', '\n'.join(methods))
    if 'TryLoadTextureDataFromCpu' not in source:
        start = harness.index('  // CPU success skips residency;')
        end = harness.index('  // Baseline: successful loads', start)
        harness = harness[:start] + harness[end:]
    with tempfile.TemporaryDirectory() as temp:
        cpp=Path(temp)/'watch.cpp';exe=Path(temp)/'watch.exe'
        cpp.write_text(harness)
        subprocess.run([args.compiler,'-std=c++20',str(cpp),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
    print('Texture watch load-race, partial-load and retry checks passed')


if __name__ == '__main__':
    main()
