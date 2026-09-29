#include <cstdio>
#include <filesystem>
#include <string>

#include "config/host_config.h"

using namespace pinyon_shift::config;

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__,   \
                   #condition);                                                 \
      ++failures;                                                               \
    }                                                                           \
  } while (false)

void TestValues() {
  const std::string text =
      "# comment\r\n"
      "pinyon_shift_config_schema = 25\r\n"
      "  vsync=true # trailing comment\r\n"
      "swap_post_effect = \"fxaa\"\r\n"
      "empty =\r\n"
      "vsync_other = false\r\n";
  CHECK(GetValue(text, "pinyon_shift_config_schema") == "25");
  CHECK(GetValue(text, "vsync") == "true");
  CHECK(GetValue(text, "swap_post_effect") == "fxaa");
  CHECK(!GetValue(text, "empty"));
  CHECK(!GetValue(text, "missing"));
  CHECK(!GetValue(text, "vsync_o"));

  // Replacing keeps the line ending and every other byte.
  const std::string replaced = SetValue(text, "vsync", "false");
  CHECK(replaced.find("vsync = false\r\n") != std::string::npos);
  CHECK(replaced.find("trailing comment") == std::string::npos);
  CHECK(replaced.size() == text.size() - std::string("  vsync=true # trailing comment").size() +
                               std::string("vsync = false").size());
  // Appending uses the file's line ending.
  CHECK(SetValue(text, "mnk_mode", "true").ends_with("vsync_other = false\r\nmnk_mode = true\r\n"));
  CHECK(SetValue("a = 1\n\n\n", "b", "2") == "a = 1\nb = 2\n");
  CHECK(SetValue("", "b", "2") == "b = 2\n");
  CHECK(Quote("fxaa") == "\"fxaa\"");
}

void TestStore() {
  const auto directory = std::filesystem::temp_directory_path() / "pinyon_shift_host_config_test";
  std::filesystem::remove_all(directory);
  const auto path = directory / "config" / "pinyon_shift.toml";
  CHECK(WriteAtomically(path, "pinyon_shift_config_schema = 25\n\n"));
  CHECK(ReadFile(path) == "pinyon_shift_config_schema = 25\n");

  HostConfig missing(directory / "missing.toml");
  CHECK(!missing.Load());
  missing.Set("vsync", "false");
  CHECK(!missing.Save() && !std::filesystem::exists(directory / "missing.toml"));

  HostConfig config(path);
  CHECK(config.Load() && !config.dirty());
  config.Set("pinyon_shift_config_schema", "25");
  CHECK(!config.dirty());
  config.Set("vsync", "false");
  CHECK(config.dirty() && config.Get("vsync") == "false");
  CHECK(config.Save() && !config.dirty());
  CHECK(ReadFile(path) == "pinyon_shift_config_schema = 25\nvsync = false\n");
  // One backup per session, taken before the first write.
  size_t backups = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory / "config" / "backups")) {
    ++backups;
    CHECK(ReadFile(entry.path()) == "pinyon_shift_config_schema = 25\n");
    CHECK(entry.path().filename().string().starts_with("pinyon_shift-"));
  }
  CHECK(backups == 1);
  config.Set("vsync", "true");
  CHECK(config.Save());
  backups = 0;
  for (const auto& entry : std::filesystem::directory_iterator(directory / "config" / "backups")) {
    (void)entry;
    ++backups;
  }
  CHECK(backups == 1);
  CHECK(!std::filesystem::exists(path.string() + ".tmp"));
  std::filesystem::remove_all(directory);
}

}  // namespace

int main(int argc, char** argv) {
  // For tools/tests/test_host_config.py: apply `name literal` pairs to a file
  // the way the settings screen does and write it.
  if (argc >= 5 && std::string(argv[1]) == "--set" && argc % 2 == 1) {
    auto text = ReadFile(argv[2]).value_or("");
    for (int i = 3; i + 1 < argc; i += 2) {
      text = SetValue(text, argv[i], argv[i + 1]);
    }
    return WriteAtomically(argv[2], text) ? 0 : 1;
  }
  if (argc == 4 && std::string(argv[1]) == "--get") {
    auto text = ReadFile(argv[2]);
    auto value = text ? GetValue(*text, argv[3]) : std::nullopt;
    std::fputs(value ? value->c_str() : "<absent>", stdout);
    return 0;
  }
  TestValues();
  TestStore();
  if (failures) {
    std::fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  std::puts("host config tests passed");
  return 0;
}
