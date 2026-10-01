#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace pinyon_shift::config {

// Edits of the host configuration (`<state>/config/pinyon_shift.toml`), a
// flat file of `name = value` lines. The rules match the launcher's
// tools/host-config.ps1, so both write the same bytes for the same edit
// (tools/tests/test_host_config.py runs them against each other):
//
// - A setting is the first line whose name, after leading spaces or tabs, is
//   followed by optional spaces or tabs and '='.
// - Its value runs to a '#' or the end of the line, trimmed, with the
//   surrounding quotes of a string removed; an empty value counts as absent.
// - Setting a value replaces that line (not its line ending) with
//   `name = value`, or appends the line using the file's line ending.
// - Files are written through a temporary file that replaces the old one,
//   and end with one line ending.
// - A backup is a copy in `config/backups/pinyon_shift-<UTC time>.toml`.

// The unquoted value of `name`, or nullopt.
std::optional<std::string> GetValue(std::string_view text, std::string_view name);
// `text` with `name = literal`; `literal` is written as given (quote strings).
std::string SetValue(std::string_view text, std::string_view name, std::string_view literal);
// A TOML literal for a string value.
std::string Quote(std::string_view value);

std::optional<std::string> ReadFile(const std::filesystem::path& path);
bool WriteAtomically(const std::filesystem::path& path, std::string_view text);
// Copies the current file into the backup directory; returns the copy.
std::optional<std::filesystem::path> Backup(const std::filesystem::path& path);

// The configuration as the in-game settings screen edits it: changes collect
// in memory and Save writes them once, with a backup taken before the first
// save of the session.
class HostConfig {
 public:
  explicit HostConfig(std::filesystem::path path);

  const std::filesystem::path& path() const { return path_; }
  // Reloads from disk, keeping nothing unsaved.
  bool Load();
  std::optional<std::string> Get(std::string_view name) const { return GetValue(text_, name); }
  void Set(std::string_view name, std::string_view literal);
  bool dirty() const { return dirty_; }
  // Writes pending changes; true when there was nothing to write. Refuses
  // when the file was never loaded, so a partial file never replaces it.
  bool Save();

 private:
  std::filesystem::path path_;
  std::string text_;
  bool loaded_ = false;
  bool dirty_ = false;
  bool backed_up_ = false;
};

}  // namespace pinyon_shift::config
