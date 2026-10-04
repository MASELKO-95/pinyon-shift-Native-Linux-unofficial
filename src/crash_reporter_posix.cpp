#include "crash_reporter.h"

// The POSIX crash reporter: a handler for the fatal signals, on its own stack,
// writes <crash_root>/<session>-<signal>.txt with the signal, the faulting
// address, the program counter (with its library and offset) and a backtrace,
// then lets the default action end the process (and leave a core dump where
// the system keeps them). Only async-signal-safe calls run in the handler,
// except dladdr() and backtrace(), which glibc, bionic and macOS support there
// once backtrace() has been called before the fault.

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <rex/exception_handler.h>

#if defined(__GLIBC__) || defined(__APPLE__) || \
    (defined(__ANDROID__) && __ANDROID_API__ >= 33)
#include <execinfo.h>
#define PINYON_SHIFT_HAVE_BACKTRACE 1
#endif
#include <dlfcn.h>

namespace pinyon_shift::diagnostics::crash {
namespace {

constexpr int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
// "<crash_root>/<session>-" prepared at install; the handler appends the
// signal name, so it never allocates.
std::array<char, 4096> g_report_prefix{};
size_t g_report_prefix_length = 0;
std::atomic_flag g_reported = ATOMIC_FLAG_INIT;
std::array<std::byte, 64 * 1024> g_signal_stack{};
bool g_runtime_reporter_installed = false;

const char* SignalName(int signal) {
  switch (signal) {
    case SIGSEGV:
      return "sigsegv";
    case SIGBUS:
      return "sigbus";
    case SIGILL:
      return "sigill";
    case SIGFPE:
      return "sigfpe";
    case SIGABRT:
      return "sigabrt";
    default:
      return "signal";
  }
}

void Write(int file, const char* text) { (void)!write(file, text, std::strlen(text)); }

void WriteHex(int file, const char* label, uint64_t value) {
  char buffer[40];
  size_t length = 0;
  for (const char* c = label; *c && length < 16; ++c) buffer[length++] = *c;
  buffer[length++] = '0';
  buffer[length++] = 'x';
  for (int shift = 60; shift >= 0; shift -= 4) {
    buffer[length++] = "0123456789ABCDEF"[(value >> shift) & 0xF];
  }
  buffer[length++] = '\n';
  (void)!write(file, buffer, length);
}

uint64_t ProgramCounter(const void* context) {
  const auto* user = static_cast<const ucontext_t*>(context);
  if (!user) return 0;
#if defined(__APPLE__) && defined(__x86_64__)
  return user->uc_mcontext->__ss.__rip;
#elif defined(__APPLE__) && defined(__aarch64__)
  return user->uc_mcontext->__ss.__pc;
#elif defined(__linux__) && defined(__x86_64__)
  return uint64_t(user->uc_mcontext.gregs[REG_RIP]);
#elif defined(__linux__) && defined(__aarch64__)
  return user->uc_mcontext.pc;
#else
  return 0;
#endif
}

void Report(int signal, uint64_t fault_address, uint64_t pc) {
  if (!g_reported.test_and_set()) {
    char path[4200];
    std::memcpy(path, g_report_prefix.data(), g_report_prefix_length);
    size_t length = g_report_prefix_length;
    for (const char* c = SignalName(signal); *c; ++c) path[length++] = *c;
    for (const char* c = ".txt"; *c; ++c) path[length++] = *c;
    path[length] = '\0';
    const int file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (file >= 0) {
      Write(file, "Pinyon Shift fatal signal\nsignal=");
      Write(file, SignalName(signal));
      Write(file, "\n");
      WriteHex(file, "fault_address=", fault_address);
      WriteHex(file, "pc=", pc);
      // The library holding the PC and the offset into it, which a
      // symbolizer needs where the load address differs per run.
      Dl_info module{};
      if (pc && dladdr(reinterpret_cast<void*>(uintptr_t(pc)), &module) && module.dli_fname) {
        Write(file, "pc_module=");
        Write(file, module.dli_fname);
        Write(file, "\n");
        WriteHex(file, "pc_offset=", pc - uint64_t(uintptr_t(module.dli_fbase)));
      }
#if defined(PINYON_SHIFT_HAVE_BACKTRACE)
      void* frames[64];
      const int count = backtrace(frames, 64);
      backtrace_symbols_fd(frames, count, file);
#endif
      close(file);
    }
  }
  // The default action ends the process as the signal would have.
  struct sigaction default_action {};
  default_action.sa_handler = SIG_DFL;
  sigemptyset(&default_action.sa_mask);
  sigaction(signal, &default_action, nullptr);
  raise(signal);
}

void Handler(int signal, siginfo_t* info, void* context) {
  Report(signal, uint64_t(uintptr_t(info ? info->si_addr : nullptr)),
         ProgramCounter(context));
}

bool UnhandledRuntimeException(rex::arch::Exception* exception, void*) {
  const int signal = exception->code() ==
                             rex::arch::Exception::Code::kIllegalInstruction
                         ? SIGILL
                         : SIGSEGV;
  Report(signal, exception->fault_address(), exception->pc());
  return true;
}

}  // namespace

void Install(const std::filesystem::path& crash_root, const std::string& session_id) {
  const std::string prefix = (crash_root / session_id).string() + "-";
  g_report_prefix_length = std::min(prefix.size(), g_report_prefix.size() - 1);
  std::memcpy(g_report_prefix.data(), prefix.data(), g_report_prefix_length);
#if defined(PINYON_SHIFT_HAVE_BACKTRACE)
  // The first backtrace() loads the unwinder; do it now, not in the handler.
  void* frame;
  backtrace(&frame, 1);
#endif
  stack_t stack{};
  stack.ss_sp = g_signal_stack.data();
  stack.ss_size = g_signal_stack.size();
  sigaltstack(&stack, nullptr);
  struct sigaction action {};
  action.sa_sigaction = Handler;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&action.sa_mask);
  for (const int signal : kSignals) {
    // A handler installed after this one (the runtime's, which serves guest
    // MMIO and write watches through SIGSEGV) keeps this one as its fallback
    // and forwards the faults it does not expect, so it must stay in front;
    // replacing it would turn every expected fault into a crash. Only an
    // absent or reset handler is (re)installed.
    struct sigaction current {};
    if (sigaction(signal, nullptr, &current) == 0 && (current.sa_flags & SA_SIGINFO) &&
        current.sa_sigaction && current.sa_sigaction != Handler) {
      continue;
    }
    if (!(current.sa_flags & SA_SIGINFO) && current.sa_handler != SIG_DFL &&
        current.sa_handler != SIG_IGN) {
      continue;
    }
    sigaction(signal, &action, nullptr);
  }
}

void Refresh() {
  // Called after runtime setup, when ReXGlue has installed its MMIO and GPU
  // write-watch handlers. These faults are recoverable: replacing the SDK's
  // sigaction would turn normal guest writes into fatal crashes. Append a
  // last-chance reporter instead, leaving the SDK first refusal on faults.
  if (!g_runtime_reporter_installed) {
    rex::arch::ExceptionHandler::Install(UnhandledRuntimeException, nullptr);
    g_runtime_reporter_installed = true;
  }
}

void RaiseAccessViolation() { raise(SIGSEGV); }

void ExecuteNull() {
  volatile uintptr_t null_target = 0;
  reinterpret_cast<void (*)()>(null_target)();
}

}  // namespace pinyon_shift::diagnostics::crash
