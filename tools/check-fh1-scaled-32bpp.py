"""Check the candidate scaled-load address and byte swap against SDK addressing."""
import argparse
from pathlib import Path
import subprocess
import tempfile
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--compiler',default='clang++');a=p.parse_args()
root=Path(__file__).resolve().parents[1]
hlsl=(root/'thirdparty/shiftglue-sdk/src/graphics/shaders/fh1_scaled_32bpp_2x.cs.hlsl').read_text()
util=(root/'thirdparty/shiftglue-sdk/src/graphics/pipeline/texture/util.cpp').read_text()
reference=util[util.index('int32_t GetTiledOffset2D('):util.index('int32_t GetTiledOffset3D(')]
functions=hlsl[hlsl.index('uint TiledOffset('):hlsl.index('[numthreads')]
cp=(root/'thirdparty/shiftglue-sdk/src/graphics/d3d12/texture_cache.cpp').read_text()
gate=cp.split('const bool fh1_scaled_32 = ',1)[1].split(';',1)[0]
assert 'if (fh1_scaled_32 && load_pipeline_fh1_scaled_32_)' in cp
stores='\n'.join(line for line in hlsl.splitlines() if 'dest[uint2(' in line).replace('uint4(', 'Pack(')
code=r'''
#include <cassert>
#include <cstdint>
#include <random>
using uint=uint32_t;using uint4=uint32_t;uint guest_pitch;
namespace rex {uint32_t align(uint32_t v,uint32_t a){return (v+a-1)&~(a-1);}}
namespace xenos {constexpr uint32_t kTextureTileWidthHeight=32;}
REFERENCE
CANDIDATE
namespace xenos {enum class DataDimension{k2DOrStacked,k3D};}
struct Key {xenos::DataDimension dimension=xenos::DataDimension::k2DOrStacked;uint depth=1,width=1280,height=720,pitch=40,endianness=2,format=7,mip_max_level=0;bool tiled=true,packed_mips=false,signed_separate=false;
 uint GetDepthOrArraySize(){return depth;}uint GetWidth(){return width;}uint GetHeight(){return height;}};
struct Texture {bool force=false;bool force_load_3d_tiling(){return force;}};
uint scale_x=2,scale_y=2;uint draw_resolution_scale_x(){return scale_x;}uint draw_resolution_scale_y(){return scale_y;}
constexpr uint kLoadShaderIndex32bpb=3;
bool admit(Key texture_key,Texture d3d12_texture,bool load_base=true,bool load_mips=false,bool texture_resolution_scaled=true,uint load_shader=kLoadShaderIndex32bpb){return GATE;}
struct uint2 {uint x,y;uint2(uint a,uint b):x(a),y(b){}};
struct Target {uint data[8]{};uint& operator[](uint2 p){assert(p.x<8&&p.y==0);return data[p.x];}};
uint Pack(uint r,uint g,uint b,uint a){assert(r<1024&&g<1024&&b<1024&&a<4);return r|(g<<10)|(b<<20)|(a<<30);}
int main(){
 std::mt19937 pack_random(2);
 for(int n=0;n<10000;++n){uint a[4],b[4];for(int i=0;i<4;++i){a[i]=pack_random();b[i]=pack_random();}Target dest;uint x=0,y=0;
 for(uint i=0;i<4;++i){STORES}
 for(uint i=0;i<4;++i){assert(dest.data[i]==a[i]);assert(dest.data[4+i]==b[i]);}}

 Key key;Texture texture;assert(admit(key,texture));
 assert(!admit(key,texture,false));assert(!admit(key,texture,true,true));assert(!admit(key,texture,true,false,false));assert(!admit(key,texture,true,false,true,0));
 for(uint x=1;x<=3;++x)for(uint y=1;y<=3;++y){scale_x=x;scale_y=y;assert(admit(key,texture)==(x==2&&y==2));}scale_x=scale_y=2;
 for(int n=0;n<12;++n){Key k;Texture t;switch(n){case 0:k.dimension=xenos::DataDimension::k3D;break;case 1:k.depth=2;break;case 2:k.width=1279;break;case 3:k.height=719;break;case 4:k.pitch=41;break;case 5:k.endianness=0;break;case 6:k.format=6;break;case 7:k.mip_max_level=1;break;case 8:k.tiled=false;break;case 9:k.packed_mips=true;break;case 10:k.signed_separate=true;break;case 11:t.force=true;break;}assert(!admit(k,t));}

 for(uint pitch: {32u,128u,1280u,2048u}){guest_pitch=pitch;
  for(uint y=0;y<1440;++y)for(uint x=0;x<pitch;x+=4)
   assert(TiledOffset(x,y)==uint(GetTiledOffset2D(x,y,pitch,2)));
 }
 std::mt19937 random(1);
 for(int i=0;i<100000;++i){uint v=random(),expected=0;for(int b=0;b<4;++b)expected|=((v>>(b*8))&255)<<((3-b)*8);assert(Swap(v)==expected);}
}
'''.replace('REFERENCE',reference).replace('CANDIDATE',functions).replace('GATE',gate).replace('STORES',stores)
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'check.cpp';exe=Path(tmp)/'check.exe';cpp.write_text(code)
    subprocess.run([a.compiler,'-std=c++20','-O2',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Scaled conversion: production HLSL address and endian expressions and live admission exclusions pass')
