// NP-3.5: a build with the FMA3 baseline (PINYON_SHIFT_CPU_BASELINE=fma)
// stops with a message on a CPU without FMA3, instead of crashing on the
// first fused multiply-add. tools/build-preview.ps1 only picks that baseline
// on a CPU that has it, so this matters when a build is copied elsewhere.
#if defined(PINYON_SHIFT_CPU_BASELINE_FMA) && defined(_WIN32)

#include <cpuid.h>
#include <cstdint>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

bool CpuRunsFma() {
  unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
  if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return false;
  constexpr unsigned int kFma = 1u << 12, kOsXsave = 1u << 27, kAvx = 1u << 28;
  if ((ecx & (kFma | kOsXsave | kAvx)) != (kFma | kOsXsave | kAvx)) return false;
  // FMA3 uses the AVX register state, which the OS must save (XCR0 bits 1, 2).
  uint32_t low = 0, high = 0;
  __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
  return (low & 0x6u) == 0x6u;
}

const bool g_cpu_checked = [] {
  if (!CpuRunsFma()) {
    MessageBoxW(nullptr,
                L"This build of Pinyon Shift uses FMA3 instructions, which this CPU does "
                L"not have. Build it again on this PC; the build chooses the instructions "
                L"the CPU supports.",
                L"Unsupported CPU", MB_OK | MB_ICONERROR);
    ExitProcess(ERROR_NOT_SUPPORTED);
  }
  return true;
}();

}  // namespace

#endif
