"""Check the production present-result path logs both HRESULTs before returning GPU loss."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', default='clang++')
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
source = (repo/'thirdparty/shiftglue-sdk/src/ui/d3d12/d3d12_presenter.cpp').read_text()
begin = source.index('  if (present_result == DXGI_ERROR_DEVICE_REMOVED')
end = source.index('\nbool D3D12Presenter::InitializeSurfaceIndependent()', begin)
body = source[begin:end]
harness = r'''
#include <cassert>
#include <cstdint>
#include <vector>
using HRESULT = int32_t;
constexpr HRESULT DXGI_ERROR_DEVICE_REMOVED = HRESULT(0x887A0005u);
constexpr HRESULT DXGI_ERROR_DEVICE_RESET = HRESULT(0x887A0007u);
#define SUCCEEDED(value) ((value) >= 0)
enum class PaintResult { kGpuLostExternally, kGpuLostResponsible, kPresented, kNotPresented };
std::vector<unsigned> events;
unsigned logged_result, logged_reason;
int reason_queries;
void Log(const char*, unsigned result, unsigned reason) {
  events.push_back(1); logged_result = result; logged_reason = reason;
}
#define REXLOG_ERROR(...) Log(__VA_ARGS__)
namespace rex { void FlushLogging() { events.push_back(2); } }
struct Device {
  HRESULT GetDeviceRemovedReason() { ++reason_queries; return HRESULT(0x887A0006u); }
} device;
struct Provider { Device* GetDevice() { return &device; } } provider_;
PaintResult Paint(HRESULT present_result) {
''' + body + r'''
int main() {
  assert(Paint(0) == PaintResult::kPresented);
  assert(Paint(HRESULT(0x80004005u)) == PaintResult::kNotPresented);
  assert(events.empty() && reason_queries == 0);
  for (auto result : {DXGI_ERROR_DEVICE_REMOVED, DXGI_ERROR_DEVICE_RESET}) {
    events.clear();
    assert(Paint(result) == (result == DXGI_ERROR_DEVICE_REMOVED
        ? PaintResult::kGpuLostExternally : PaintResult::kGpuLostResponsible));
    assert((events == std::vector<unsigned>{1, 2}));
    assert(logged_result == unsigned(result) && logged_reason == 0x887A0006u);
  }
  assert(reason_queries == 2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path/'check.cpp').write_text(harness)
    subprocess.run([args.compiler, '-std=c++20', str(path/'check.cpp'), '-o', str(path/'check.exe')], check=True)
    subprocess.run([str(path/'check.exe')], check=True)
print('present success/failure classification and flushed GPU-loss HRESULTs passed')
