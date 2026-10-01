#include "platform/host_platform.h"

#include <cstdio>
#include <cstdlib>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <pthread.h>
#include <unistd.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace pinyon_shift::platform {

bool ReplaceFileAtomically(const std::filesystem::path& temporary,
                           const std::filesystem::path& destination) {
  std::error_code error;
#if defined(_WIN32)
  if (MoveFileExW(temporary.c_str(), destination.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return true;
  }
#else
  // rename(2) replaces the destination atomically on POSIX file systems.
  std::filesystem::rename(temporary, destination, error);
  if (!error) {
    return true;
  }
#endif
  std::filesystem::remove(temporary, error);
  return false;
}

std::optional<std::string> EnvironmentVariable(const char* name) {
#if defined(_WIN32)
  const int name_length = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
  if (name_length <= 0) {
    return std::nullopt;
  }
  std::wstring wide_name(size_t(name_length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_name.data(), name_length);
  // The size includes the terminator, so a variable set to "" gives 1.
  const DWORD size = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
  if (!size) {
    return std::nullopt;
  }
  std::wstring value(size, L'\0');
  const DWORD length = GetEnvironmentVariableW(wide_name.c_str(), value.data(), size);
  if (length >= size) {
    return std::nullopt;
  }
  value.resize(length);
  const int utf8_length =
      WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr,
                          nullptr);
  std::string result(size_t(utf8_length), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(), utf8_length,
                      nullptr, nullptr);
  return result;
#else
  const char* value = std::getenv(name);
  if (!value) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

bool EnvironmentFlag(const char* name) {
  const auto value = EnvironmentVariable(name);
  return value && *value == "1";
}

std::optional<std::filesystem::path> EnvironmentPath(const char* name) {
  const auto value = EnvironmentVariable(name);
  if (!value || value->empty()) {
    return std::nullopt;
  }
  const std::filesystem::path path(
      std::u8string(reinterpret_cast<const char8_t*>(value->data()), value->size()));
  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  return (error ? path : absolute).lexically_normal();
}

std::filesystem::path ExecutablePath() {
#if defined(_WIN32)
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length) {
      return {};
    }
    if (length < buffer.size()) {
      return std::filesystem::path(std::wstring(buffer.data(), length));
    }
    buffer.resize(buffer.size() * 2);
  }
#elif defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }
  std::error_code error;
  auto path = std::filesystem::canonical(buffer.c_str(), error);
  return error ? std::filesystem::path(buffer.c_str()) : path;
#else
  std::error_code error;
  auto path = std::filesystem::read_symlink("/proc/self/exe", error);
  return error ? std::filesystem::path() : path;
#endif
}

uint64_t ProcessId() {
#if defined(_WIN32)
  return GetCurrentProcessId();
#else
  return uint64_t(getpid());
#endif
}

uint64_t ThreadId() {
#if defined(_WIN32)
  return GetCurrentThreadId();
#elif defined(__APPLE__)
  uint64_t id = 0;
  pthread_threadid_np(nullptr, &id);
  return id;
#else
  return uint64_t(syscall(SYS_gettid));
#endif
}

#if defined(_WIN32)
namespace {
std::wstring Widen(std::string_view text) {
  const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
  std::wstring result(size_t(length), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), length);
  return result;
}
}  // namespace
#endif

void ShowFatalError(std::string_view title, std::string_view text) {
#if defined(_WIN32)
  MessageBoxW(nullptr, Widen(text).c_str(), Widen(title).c_str(), MB_OK | MB_ICONERROR);
#else
  std::fprintf(stderr, "%.*s: %.*s\n", int(title.size()), title.data(), int(text.size()),
               text.data());
#endif
}

void ExitImmediately(uint32_t code) {
#if defined(_WIN32)
  ExitProcess(code);
#else
  _exit(int(code));
#endif
}

std::optional<uint32_t> DisplayRefreshRate(void* native_window) {
#if defined(_WIN32)
  const HWND hwnd = static_cast<HWND>(native_window);
  const HMONITOR monitor = hwnd ? MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST) : nullptr;
  MONITORINFOEXW monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (monitor && GetMonitorInfoW(monitor, &monitor_info) &&
      EnumDisplaySettingsW(monitor_info.szDevice, ENUM_CURRENT_SETTINGS, &mode) &&
      mode.dmDisplayFrequency > 1) {
    return uint32_t(mode.dmDisplayFrequency);
  }
  return std::nullopt;
#else
  (void)native_window;
  return std::nullopt;
#endif
}

}  // namespace pinyon_shift::platform
