/*
 * Pinyon Shift mod ABI (NP-7).
 *
 * A mod is a DLL under <state>/mods/<name>/code/ with a mod.toml beside it.
 * It exports two C functions:
 *
 *   uint32_t rex_mod_abi_version(void);          returns PINYON_MOD_ABI_VERSION
 *   int rex_mod_create(const PinyonModApi* api,  fills `mod`, returns 0 on
 *                      PinyonMod* mod);          success
 *
 * Everything crosses the boundary as C: plain structs, function pointers and
 * UTF-8 strings, so a mod can be built with any compiler. Guest addresses are
 * never hard-coded in a mod: they come from find_symbol, keyed by the name in
 * the host's symbol table for the supported executable. Mods are loaded
 * before the title starts and are never unloaded.
 *
 * Threads: hook callbacks run on the guest thread that reached the hook;
 * guest tasks run on the title's main thread at the next frame; UI callbacks
 * (binds, dialog results) run on the UI thread. Only a guest task may call
 * call_guest.
 */
#ifndef PINYON_MOD_H_
#define PINYON_MOD_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#define PINYON_MOD_EXPORT __declspec(dllexport)
#else
#define PINYON_MOD_EXPORT __attribute__((visibility("default")))
#endif

#define PINYON_MOD_ABI_VERSION 1u

/* Hook points, by semantic id. */
typedef enum PinyonHook {
  /* Once per title frame on the main thread; no arguments. Guest tasks
     queued with enqueue_guest_task run here. */
  PINYON_HOOK_FRAME_TICK = 1,
  /* The player's vehicle pose was written: floats[0..2] = x, y, z;
     args[0] = guest address of the position (float4, big-endian). */
  PINYON_HOOK_VEHICLE_POSE = 2,
  /* The save body is about to be encrypted: args[0] = guest address of the
     plaintext body, args[1] = its size in bytes. The body may be edited in
     place (keep its size). */
  PINYON_HOOK_SAVE_BEFORE_ENCRYPT = 3,
  /* The title opened a file: text = lower-case guest path. */
  PINYON_HOOK_FILE_OPEN = 4,
  /* The pause menu built a button: args[0] = guest address of the button. */
  PINYON_HOOK_PAUSE_BUTTON_CONSTRUCTED = 5,
  /* A saved file was just decrypted, before the title parses it:
     args[0] = guest address of the plaintext body, args[1] = its size. The
     body may be edited in place (keep its size); the profile's first
     section is described in docs/MODDING.md. */
  PINYON_HOOK_SAVE_AFTER_DECRYPT = 6,
} PinyonHook;

typedef struct PinyonHookEvent {
  uint32_t hook;      /* a PinyonHook */
  uint32_t args[6];
  float floats[4];
  const char* text;   /* or NULL */
} PinyonHookEvent;

typedef void (*PinyonHookCallback)(void* user, const PinyonHookEvent* event);
typedef void (*PinyonGuestTask)(void* user);
typedef void (*PinyonBindCallback)(void* user);
/* button: index chosen, or UINT32_MAX when cancelled. */
typedef void (*PinyonDialogCallback)(void* user, uint32_t button);

typedef enum PinyonLogLevel {
  PINYON_LOG_DEBUG = 0,
  PINYON_LOG_INFO = 1,
  PINYON_LOG_WARNING = 2,
  PINYON_LOG_ERROR = 3,
} PinyonLogLevel;

/* What the host offers. `size` is sizeof(PinyonModApi) of the host that
   built it; later ABI versions only append members. */
typedef struct PinyonModApi {
  uint32_t abi_version;
  uint32_t size;
  const char* mod_name;        /* this mod's name from mod.toml */
  const char* mod_directory;   /* UTF-8 path of <state>/mods/<name> */

  void (*log)(PinyonLogLevel level, const char* text);
  /* A diagnostics event with key/value string pairs (count pairs). */
  void (*log_event)(const char* event, const char* const* keys, const char* const* values,
                    uint32_t count);

  /* Guest memory, big-endian as the title sees it. Return 0 on success. */
  int (*read_guest)(uint32_t address, void* out, uint32_t size);
  int (*write_guest)(uint32_t address, const void* data, uint32_t size);

  /* Address of a named function, hook site or global in the symbol table
     for the running executable; 0 when unknown. */
  uint32_t (*find_symbol)(const char* name);
  /* A struct field offset from the symbol table ("vehicle.pose.x"); returns
     -1 when unknown. */
  int32_t (*find_offset)(const char* name);

  /* Returns a handle for unsubscribe, 0 on failure. */
  uint64_t (*subscribe)(uint32_t hook, PinyonHookCallback callback, void* user);
  void (*unsubscribe)(uint64_t handle);

  /* Runs `task` on the title's main thread at the next frame tick. */
  void (*enqueue_guest_task)(PinyonGuestTask task, void* user);
  /* Calls a guest function with up to 6 integer arguments and returns r3.
     Only valid inside a guest task. */
  uint32_t (*call_guest)(uint32_t address, const uint32_t* args, uint32_t count);

  /* Host settings (cvars), as text. A mod registers its own with a name it
     owns (prefix it with the mod name). Return 0 on success. */
  int (*register_cvar)(const char* name, const char* default_value, const char* description);
  int (*get_cvar)(const char* name, char* out, uint32_t out_size);
  int (*set_cvar)(const char* name, const char* value);

  /* A key bind the player can rebind ("F9", "Ctrl+F9"). */
  int (*register_bind)(const char* name, const char* default_key, const char* description,
                       PinyonBindCallback callback, void* user);
  /* A host message box over the title; the result arrives on the UI thread. */
  void (*show_dialog)(const char* title, const char* text, const char* const* buttons,
                      uint32_t button_count, PinyonDialogCallback callback, void* user);

  /* UI extensions (NP-11), drawn by the host over the title in its fonts.
     A HUD label: `id` names it (ids are shared by all mods: use a range
     derived from your mod, such as a hash of its name); x and y are in the
     title's 1280x720 layout (inside the 90 % safe area is 64..1216 by
     36..684); `size` is the text height in the same units. NULL or empty
     `text` removes it. Callable from any thread. */
  void (*set_hud_text)(uint32_t id, const char* text, float x, float y, float size);
  /* An action row in SETTINGS > MOD ACTIONS (the pause menu's SETTINGS);
     `callback` runs on the UI thread when the player picks it. Returns 0. */
  int (*add_menu_action)(const char* label, PinyonBindCallback callback, void* user);
} PinyonModApi;

/* What a mod provides. Any callback may be NULL. */
typedef struct PinyonMod {
  void* self;
  /* Presentation is ready; add dialogs, binds and settings here. */
  void (*on_create_dialogs)(void* self);
  /* The title's executable is about to start. */
  void (*on_module_launched)(void* self);
  /* The host is shutting down; the title no longer runs. */
  void (*on_shutdown)(void* self);
} PinyonMod;

typedef uint32_t (*PinyonModAbiVersionFn)(void);
typedef int (*PinyonModCreateFn)(const PinyonModApi* api, PinyonMod* mod);

#ifdef __cplusplus
}
#endif

#endif /* PINYON_MOD_H_ */
