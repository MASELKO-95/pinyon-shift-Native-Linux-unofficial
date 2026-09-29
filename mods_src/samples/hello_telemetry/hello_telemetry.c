/*
 * hello_telemetry: the smallest useful Pinyon Shift mod (NP-7.6).
 *
 * - registers a setting (hello_telemetry_greeting) and a key bind (F9);
 * - counts frames on frame.tick and keeps the vehicle position from
 *   vehicle.pose_written;
 * - calls a guest function through the guest task queue: the title's
 *   XGetAVPack import (kernel.get_av_pack), which only returns a number;
 * - F9 shows a host dialog with what it saw; with hello_telemetry_self_test
 *   on it also shows it once by itself after 10 seconds, for scripted tests;
 * - logs mod.hello_telemetry.* diagnostics events, including at shutdown.
 *
 * It never writes guest memory, so it is safe on any profile.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pinyon_mod.h"

typedef struct Telemetry {
  const PinyonModApi* api;
  uint64_t frames;
  float position[3];
  int have_position;
  uint32_t av_pack;
  int av_pack_read;
  int self_test_shown;
} Telemetry;

static Telemetry g_telemetry;

static void LogEvent(const char* event, const char* key, const char* value) {
  const char* keys[1] = {key};
  const char* values[1] = {value};
  g_telemetry.api->log_event(event, keys, values, key ? 1u : 0u);
}

static void OnDialogClosed(void* user, uint32_t button) {
  char text[32];
  (void)user;
  snprintf(text, sizeof(text), "%d", button == UINT32_MAX ? -1 : (int)button);
  LogEvent("hello_telemetry.dialog_closed", "button", text);
}

static void ShowTelemetry(void* user) {
  char greeting[128];
  char text[512];
  const char* buttons[1] = {"OK"};
  (void)user;
  if (g_telemetry.api->get_cvar("hello_telemetry_greeting", greeting, sizeof(greeting)) != 0) {
    strcpy(greeting, "Hello");
  }
  snprintf(text, sizeof(text),
           "%s. The title has run %llu frames. The car is at %.1f, %.1f, %.1f. XGetAVPack "
           "returned %u.",
           greeting, (unsigned long long)g_telemetry.frames, g_telemetry.position[0],
           g_telemetry.position[1], g_telemetry.position[2], g_telemetry.av_pack);
  g_telemetry.api->show_dialog("Hello telemetry", text, buttons, 1, OnDialogClosed, NULL);
  LogEvent("hello_telemetry.dialog_shown", NULL, NULL);
}

static void ReadAvPack(void* user) {
  /* Runs on the title's main thread at a frame tick: call_guest is valid. */
  const uint32_t address = g_telemetry.api->find_symbol("kernel.get_av_pack");
  char text[32];
  (void)user;
  if (!address) {
    g_telemetry.api->log(PINYON_LOG_WARNING, "hello_telemetry: kernel.get_av_pack is unknown");
    return;
  }
  g_telemetry.av_pack = g_telemetry.api->call_guest(address, NULL, 0);
  g_telemetry.av_pack_read = 1;
  snprintf(text, sizeof(text), "%u", g_telemetry.av_pack);
  LogEvent("hello_telemetry.av_pack", "value", text);
}

static void OnFrame(void* user, const PinyonHookEvent* event) {
  char self_test[8];
  (void)user;
  (void)event;
  ++g_telemetry.frames;
  if (g_telemetry.frames == 60) {
    g_telemetry.api->enqueue_guest_task(ReadAvPack, NULL);
  }
  /* About 10 s in at the console's 60 frames a second. */
  if (!g_telemetry.self_test_shown && g_telemetry.frames >= 600 &&
      g_telemetry.api->get_cvar("hello_telemetry_self_test", self_test, sizeof(self_test)) ==
          0 &&
      strcmp(self_test, "true") == 0) {
    g_telemetry.self_test_shown = 1;
    ShowTelemetry(NULL);
  }
}

static void OnPose(void* user, const PinyonHookEvent* event) {
  (void)user;
  memcpy(g_telemetry.position, event->floats, sizeof(g_telemetry.position));
  g_telemetry.have_position = 1;
}

static void OnCreateDialogs(void* self) {
  (void)self;
  g_telemetry.api->register_bind("hello_telemetry_show", "F9",
                                 "hello_telemetry: show what the mod saw", ShowTelemetry, NULL);
}

static void OnModuleLaunched(void* self) {
  (void)self;
  LogEvent("hello_telemetry.launched", NULL, NULL);
}

static void OnShutdown(void* self) {
  char frames[32];
  (void)self;
  snprintf(frames, sizeof(frames), "%llu", (unsigned long long)g_telemetry.frames);
  LogEvent("hello_telemetry.shutdown", "frames", frames);
}

PINYON_MOD_EXPORT uint32_t rex_mod_abi_version(void) { return PINYON_MOD_ABI_VERSION; }

PINYON_MOD_EXPORT int rex_mod_create(const PinyonModApi* api, PinyonMod* mod) {
  if (!api || api->abi_version != PINYON_MOD_ABI_VERSION || api->size < sizeof(PinyonModApi)) {
    return 1;
  }
  memset(&g_telemetry, 0, sizeof(g_telemetry));
  g_telemetry.api = api;
  api->register_cvar("hello_telemetry_greeting", "Hello from a mod",
                     "hello_telemetry: the first words of its dialog");
  api->register_cvar("hello_telemetry_self_test", "false",
                     "hello_telemetry: show the dialog once by itself (scripted tests)");
  if (!api->subscribe(PINYON_HOOK_FRAME_TICK, OnFrame, NULL) ||
      !api->subscribe(PINYON_HOOK_VEHICLE_POSE, OnPose, NULL)) {
    return 1;
  }
  mod->self = &g_telemetry;
  mod->on_create_dialogs = OnCreateDialogs;
  mod->on_module_launched = OnModuleLaunched;
  mod->on_shutdown = OnShutdown;
  api->log(PINYON_LOG_INFO, "hello_telemetry: created");
  return 0;
}
