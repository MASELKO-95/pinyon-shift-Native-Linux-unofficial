#include "stall_dump.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>
#include <rex/system/thread_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

#include "pinyon_shift_diagnostics.h"

REXCVAR_DEFINE_INT32(pinyon_shift_stall_dump_seconds, 20, "Pinyon Shift",
                     "Seconds without a title frame before the log gets every guest thread's "
                     "registers and guest call stack, once per stall; 0 turns it off");

namespace {

std::atomic<int64_t> g_last_frame_ns{0};
std::atomic<uint64_t> g_frames{0};

int64_t NowNs() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

bool Readable(rex::memory::Memory* memory, uint32_t address) {
  if (address < 0x1000 || (address & 3) != 0) return false;
  auto* heap = memory->LookupHeap(address);
  if (!heap ||
      heap->QueryRangeAccess(address, address + 3) == rex::memory::PageAccess::kNoAccess) {
    return false;
  }
  return rex::memory::IsHostReadable(memory->TranslateVirtual(address));
}

uint32_t Load32(rex::memory::Memory* memory, uint32_t address) {
  return static_cast<uint32_t>(
      *rex::memory::GuestPtr<rex::be_u32*>(memory->virtual_membase(), address));
}

// Each thread's last stored registers (current at its last kernel call, which
// is where a stalled thread sits) and its guest back chain: the title saves
// the caller's link register 8 bytes below the caller's frame.
void DumpThreads() {
  auto* kernel = rex::system::kernel_state();
  if (!kernel) return;
  auto* memory = kernel->memory();
  auto threads = kernel->object_table()->GetObjectsByType<rex::system::XThread>();
  REXLOG_WARN("Stall: {:.1f} s since the last title frame; {} threads",
              double(NowNs() - g_last_frame_ns.load(std::memory_order_acquire)) / 1e9,
              threads.size());
  for (const auto& thread : threads) {
    if (!thread->is_guest_thread() || !thread->thread_state()) continue;
    const PPCContext* c = thread->thread_state()->context();
    if (!c) continue;
    REXLOG_WARN(
        "Stall: thread {:X} '{}' r1={:08X} lr={:08X} r3={:08X} r4={:08X} r5={:08X} r6={:08X} "
        "r7={:08X} r8={:08X} r30={:08X} r31={:08X}",
        thread->thread_id(), thread->name(), c->r1.u32, uint32_t(c->lr), c->r3.u32, c->r4.u32,
        c->r5.u32, c->r6.u32, c->r7.u32, c->r8.u32, c->r30.u32, c->r31.u32);
    uint32_t sp = c->r1.u32;
    for (int depth = 0; depth < 24 && Readable(memory, sp); ++depth) {
      const uint32_t back = Load32(memory, sp);
      if (back <= sp || back - sp > 0x10000 || !Readable(memory, back - 8)) break;
      const uint32_t saved_lr = Load32(memory, back - 8);
      // The frame's first words: the title spills arguments there, so a
      // stalled call's objects can be read back.
      std::string words;
      const uint32_t count = std::min<uint32_t>((back - sp) / 4, 72);
      for (uint32_t i = 0; i < count; ++i) {
        if (!Readable(memory, sp + 4 * i)) break;
        words += fmt::format("{}{:08X}", i ? " " : "", Load32(memory, sp + 4 * i));
      }
      REXLOG_WARN("Stall:   #{} sp={:08X} returns to {:08X}: {}", depth, sp, saved_lr, words);
      sp = back;
    }
  }
}

void Watchdog() {
  int64_t dumped_for = -1;
  // On demand too: a stall can keep the title's frames ticking while nothing
  // reaches the screen. Creating logs/dump-threads asks for one dump.
  const auto trigger = pinyon_shift::diagnostics::StateRoot() / "logs" / "dump-threads";
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    std::error_code error;
    if (std::filesystem::remove(trigger, error)) {
      DumpThreads();
    }
    const int32_t seconds = REXCVAR_GET(pinyon_shift_stall_dump_seconds);
    if (seconds <= 0) continue;
    const int64_t last = g_last_frame_ns.load(std::memory_order_acquire);
    if (last == dumped_for) continue;
    if (NowNs() - last < int64_t(seconds) * 1000000000) continue;
    dumped_for = last;
    DumpThreads();
  }
}

}  // namespace

namespace pinyon_shift::stall {

void NoteFrame() {
  g_last_frame_ns.store(NowNs(), std::memory_order_release);
  static std::once_flag started;
  std::call_once(started, [] { std::thread(Watchdog).detach(); });
}

}  // namespace pinyon_shift::stall
