// Samples named threads of a running process and records their call stacks,
// without administrator rights or ETW: it suspends each thread only to copy
// its registers and the top 64 KB of its stack, resumes it, then walks the copy
// with DbgHelp, and records the CPU cycles the thread used since the previous
// sample. Symbols come from the modules' PDBs.
//
//   pinyon_shift_thread_sampler --process pinyon_shift.exe --thread "GPU Commands"
//       [--thread <description>...] [--interval-us 2000] [--lines 1]
//       [--output <directory>]
//
// --lines 1 adds each leaf's source file and line to its name.
//
// It waits for the process and a matching thread, adds matching threads that
// start later, samples until the process exits and writes, in <directory>,
// samples.csv (t_ms, thread, cycles, stack_id), stacks.csv (stack_id, frames
// root first, ';' separated) and threads.csv (thread, name).
// tools/summarize-thread-samples.py reports a time window of them.

#include <windows.h>

#include <dbghelp.h>
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct SampledThread {
  DWORD id = 0;
  std::string name;
  HANDLE handle = nullptr;
  ULONG64 cycles = 0;
};

std::string Narrow(const std::wstring& text) {
  if (text.empty()) {
    return {};
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0,
                                       nullptr, nullptr);
  std::string result(size_t(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size, nullptr,
                      nullptr);
  return result;
}

DWORD FindProcess(const std::wstring& name) {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return 0;
  }
  PROCESSENTRY32W entry{sizeof(entry)};
  DWORD id = 0;
  for (BOOL ok = Process32FirstW(snapshot, &entry); ok; ok = Process32NextW(snapshot, &entry)) {
    if (_wcsicmp(entry.szExeFile, name.c_str()) == 0) {
      id = entry.th32ProcessID;
      break;
    }
  }
  CloseHandle(snapshot);
  return id;
}

// Opens every thread of `process_id` whose description is one of `names`.
std::vector<SampledThread> FindThreads(DWORD process_id, const std::vector<std::wstring>& names) {
  std::vector<SampledThread> threads;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snapshot == INVALID_HANDLE_VALUE) {
    return threads;
  }
  THREADENTRY32 entry{sizeof(entry)};
  for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
    if (entry.th32OwnerProcessID != process_id) {
      continue;
    }
    HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                   THREAD_QUERY_LIMITED_INFORMATION,
                               FALSE, entry.th32ThreadID);
    if (!handle) {
      continue;
    }
    PWSTR description = nullptr;
    std::wstring name;
    if (SUCCEEDED(GetThreadDescription(handle, &description)) && description) {
      name = description;
      LocalFree(description);
    }
    // Guest-visible threads carry their handle after the name, as in
    // "GPU Commands (F8000018)", so a description matches by prefix.
    bool wanted = false;
    for (const auto& wanted_name : names) {
      wanted |= name.starts_with(wanted_name);
    }
    if (wanted) {
      SampledThread thread;
      thread.id = entry.th32ThreadID;
      thread.name = Narrow(name);
      thread.handle = handle;
      QueryThreadCycleTime(handle, &thread.cycles);
      threads.push_back(thread);
    } else {
      CloseHandle(handle);
    }
  }
  CloseHandle(snapshot);
  return threads;
}

class Symbolizer {
 public:
  explicit Symbolizer(HANDLE process) : process_(process) {}

  const std::string& Name(DWORD64 address) {
    auto it = names_.find(address);
    if (it != names_.end()) {
      return it->second;
    }
    std::string name;
    IMAGEHLP_MODULEW64 module{sizeof(module)};
    if (SymGetModuleInfoW64(process_, address, &module)) {
      name = Narrow(module.ModuleName);
    } else {
      name = "?";
    }
    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    DWORD64 displacement = 0;
    // SymFromAddr returns the nearest preceding symbol even past its end
    // (code without debug information); name such addresses by offset.
    if (SymFromAddr(process_, address, &displacement, symbol) &&
        (!symbol->Size || displacement < symbol->Size)) {
      name += '!';
      name.append(symbol->Name, symbol->NameLen);
    } else {
      char offset[32];
      std::snprintf(offset, sizeof(offset), "+0x%llx",
                    (unsigned long long)(address - module.BaseOfImage));
      name += offset;
    }
    // The stack table is ';' separated and quoted.
    for (char& c : name) {
      if (c == ';' || c == '"' || c == '\n' || c == '\r') {
        c = '_';
      }
    }
    return names_.emplace(address, std::move(name)).first->second;
  }

  // The function and source line, for leaves with --lines.
  const std::string& NameWithLine(DWORD64 address) {
    auto it = lines_.find(address);
    if (it != lines_.end()) {
      return it->second;
    }
    std::string name = Name(address);
    IMAGEHLP_LINE64 line{sizeof(line)};
    DWORD displacement = 0;
    if (SymGetLineFromAddr64(process_, address, &displacement, &line) && line.FileName) {
      const char* file = line.FileName;
      for (const char* c = line.FileName; *c; ++c) {
        if (*c == '\\' || *c == '/') {
          file = c + 1;
        }
      }
      name += " @";
      name += file;
      name += ':';
      name += std::to_string(line.LineNumber);
    }
    return lines_.emplace(address, std::move(name)).first->second;
  }

 private:
  HANDLE process_;
  std::unordered_map<DWORD64, std::string> names_;
  std::unordered_map<DWORD64, std::string> lines_;
};

// A copy of the sampled thread's stack, taken while it is suspended, so it can
// run again before the (slow, many-read) walk. Other memory the walk reads is
// code and unwind data, which does not change.
struct StackCopy {
  DWORD64 base = 0;
  std::vector<uint8_t> bytes;
};
StackCopy g_stack_copy;

BOOL CALLBACK ReadFromStackCopy(HANDLE process, DWORD64 address, PVOID buffer, DWORD size,
                                LPDWORD read) {
  const StackCopy& copy = g_stack_copy;
  if (address >= copy.base && address + size <= copy.base + copy.bytes.size()) {
    std::memcpy(buffer, copy.bytes.data() + (address - copy.base), size);
    *read = size;
    return TRUE;
  }
  SIZE_T bytes_read = 0;
  const BOOL ok = ReadProcessMemory(process, LPCVOID(address), buffer, size, &bytes_read);
  *read = DWORD(bytes_read);
  return ok;
}

// Captures a suspended thread's registers and the top of its stack.
bool CaptureThread(HANDLE process, HANDLE thread, CONTEXT& context) {
  context = {};
  context.ContextFlags = CONTEXT_FULL;
  if (!GetThreadContext(thread, &context)) {
    return false;
  }
  constexpr size_t kMaxStackCopy = 64 * 1024;
  MEMORY_BASIC_INFORMATION region{};
  size_t size = kMaxStackCopy;
  if (VirtualQueryEx(process, LPCVOID(context.Rsp), &region, sizeof(region))) {
    const DWORD64 end = DWORD64(region.BaseAddress) + region.RegionSize;
    size = size_t(std::min<DWORD64>(kMaxStackCopy, end - context.Rsp));
  }
  g_stack_copy.base = context.Rsp;
  g_stack_copy.bytes.resize(size);
  SIZE_T bytes_read = 0;
  if (!ReadProcessMemory(process, LPCVOID(context.Rsp), g_stack_copy.bytes.data(), size,
                         &bytes_read)) {
    bytes_read = 0;
  }
  g_stack_copy.bytes.resize(bytes_read);
  return true;
}

// Walks a captured thread's stack, leaf first.
void WalkStack(HANDLE process, HANDLE thread, CONTEXT context, std::vector<DWORD64>& frames) {
  frames.clear();
  STACKFRAME64 frame{};
  frame.AddrPC.Offset = context.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = context.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = context.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  for (int depth = 0; depth < 96; ++depth) {
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                     ReadFromStackCopy,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
        !frame.AddrPC.Offset) {
      break;
    }
    frames.push_back(frame.AddrPC.Offset);
  }
}

int Usage() {
  std::fprintf(stderr,
               "usage: pinyon_shift_thread_sampler --process <exe name> --thread <description> "
               "[--thread <description>...] [--interval-us N] [--lines 1] "
               "[--output <directory>]\n");
  return 2;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::wstring process_name;
  std::vector<std::wstring> thread_names;
  uint32_t interval_us = 2000;
  std::filesystem::path output = L".";
  bool lines = false;
  for (int i = 1; i < argc; ++i) {
    const std::wstring arg = argv[i];
    if (i + 1 >= argc) {
      return Usage();
    }
    if (arg == L"--process") {
      process_name = argv[++i];
    } else if (arg == L"--thread") {
      thread_names.push_back(argv[++i]);
    } else if (arg == L"--interval-us") {
      interval_us = uint32_t(std::wcstoul(argv[++i], nullptr, 10));
    } else if (arg == L"--lines") {
      lines = std::wcstoul(argv[++i], nullptr, 10) != 0;
    } else if (arg == L"--output") {
      output = argv[++i];
    } else {
      return Usage();
    }
  }
  if (process_name.empty() || thread_names.empty() || interval_us < 100) {
    return Usage();
  }

  // A process that exits before all the threads exist (a launcher probe, a
  // failed start) is skipped for the next one.
  DWORD process_id = 0;
  HANDLE process = nullptr;
  std::vector<SampledThread> threads;
  while (threads.size() < thread_names.size()) {
    for (auto& thread : threads) {
      CloseHandle(thread.handle);
    }
    threads.clear();
    if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) {
      CloseHandle(process);
      process = nullptr;
      Sleep(500);
    }
    if (!process) {
      while (!(process_id = FindProcess(process_name))) {
        Sleep(50);
      }
      process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE,
                            process_id);
      if (!process) {
        std::fprintf(stderr, "cannot open process %lu: %lu\n", process_id, GetLastError());
        return 1;
      }
    }
    threads = FindThreads(process_id, thread_names);
    Sleep(100);
  }
  for (const auto& thread : threads) {
    std::printf("sampling thread %lu (%s)\n", thread.id, thread.name.c_str());
  }
  std::fflush(stdout);

  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
  if (!SymInitializeW(process, nullptr, TRUE)) {
    std::fprintf(stderr, "SymInitialize failed: %lu\n", GetLastError());
    return 1;
  }
  Symbolizer symbolizer(process);

  std::filesystem::create_directories(output);
  std::ofstream samples(output / "samples.csv", std::ios::binary);
  samples << "t_ms,thread,cycles,stack_id\n";
  std::unordered_map<std::string, uint32_t> stack_ids;
  std::vector<const std::string*> stacks;

  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_ALL_ACCESS);
  const auto start = std::chrono::steady_clock::now();
  std::vector<DWORD64> frames;
  std::string stack;
  uint64_t sample_count = 0;
  std::mt19937 random(12345);
  std::uniform_real_distribution<double> jitter(0.5, 1.5);
  DWORD last_refresh = GetTickCount();
  while (WaitForSingleObject(process, 0) != WAIT_OBJECT_0) {
    if (GetTickCount() - last_refresh > 2000) {
      // Pick up modules loaded and matching threads started since the last
      // refresh (guest threads start throughout the run).
      SymRefreshModuleList(process);
      for (auto& found : FindThreads(process_id, thread_names)) {
        bool known = false;
        for (const auto& thread : threads) {
          known |= thread.id == found.id;
        }
        if (known) {
          CloseHandle(found.handle);
        } else {
          threads.push_back(found);
        }
      }
      last_refresh = GetTickCount();
    }
    for (auto& thread : threads) {
      if (SuspendThread(thread.handle) == DWORD(-1)) {
        continue;
      }
      ULONG64 cycles = 0;
      QueryThreadCycleTime(thread.handle, &cycles);
      CONTEXT context;
      const bool captured = CaptureThread(process, thread.handle, context);
      ResumeThread(thread.handle);
      if (captured) {
        WalkStack(process, thread.handle, context, frames);
      } else {
        frames.clear();
      }
      const ULONG64 used = cycles - thread.cycles;
      thread.cycles = cycles;
      if (frames.empty()) {
        continue;
      }
      stack.clear();
      for (size_t i = frames.size(); i-- > 0;) {
        // Return addresses point after the call; name the call's function.
        stack += i == 0 ? (lines ? symbolizer.NameWithLine(frames[i]) : symbolizer.Name(frames[i]))
                        : symbolizer.Name(frames[i] - 1);
        if (i) {
          stack += ';';
        }
      }
      auto [it, inserted] = stack_ids.emplace(stack, uint32_t(stacks.size()));
      if (inserted) {
        stacks.push_back(&it->first);
      }
      const double t_ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      char line[96];
      std::snprintf(line, sizeof(line), "%.3f,%lu,%llu,%u\n", t_ms, thread.id,
                    (unsigned long long)used, it->second);
      samples << line;
      ++sample_count;
    }
    LARGE_INTEGER due;
    // A fixed period locks onto the sampled threads' timer-driven wake-ups
    // (vblank, sleeps) and over-samples the code that runs right after them;
    // a uniformly jittered period does not.
    due.QuadPart = -int64_t(jitter(random) * interval_us * 10);
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
      WaitForSingleObject(timer, INFINITE);
    } else {
      Sleep(interval_us / 1000);
    }
  }

  std::ofstream stack_file(output / "stacks.csv", std::ios::binary);
  stack_file << "stack_id,stack\n";
  for (size_t i = 0; i < stacks.size(); ++i) {
    stack_file << i << ",\"" << *stacks[i] << "\"\n";
  }
  std::ofstream threads_file(output / "threads.csv", std::ios::binary);
  threads_file << "thread,name\n";
  for (const auto& thread : threads) {
    threads_file << thread.id << ',' << thread.name << '\n';
    CloseHandle(thread.handle);
  }
  std::printf("%llu samples, %zu stacks\n", (unsigned long long)sample_count, stacks.size());
  SymCleanup(process);
  CloseHandle(process);
  return 0;
}
