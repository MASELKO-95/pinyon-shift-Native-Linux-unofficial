# Disc edition and game language

## Before installation

Choose your ISO, confirm ownership and choose **Disc edition**:

- **Auto-detect supported retail disc** checks the upstream retail allowlist.
- **USA — retail** requires that specific supported disc's size and SHA-256.
- **Europe (owner-reported Polish retail edition) — experimental** accepts
  only the recorded reconstructed Polish NXE image. It is not a generic
  acceptance option for every Polish or European disc.

Both the ISO and all three extracted executable modules must match the
manifest. A wrong selection or unknown image is rejected before translation.
The existing build's edition is recognized from its executable hash for
rebuilds. There is one installed game edition per source tree; switching
editions requires verification and rebuilding, not selecting a different
language. Saves remain outside the extraction/build directories.

## Before playing

Select **Game language** on the ready screen, then press **Play**. The launcher
stores the matching console language and country together, and remembers
them for the next start. **Polski** uses language 11 and country 82. The
options mirror the original game's F6 profile-language settings, including
English, Polish, French, German, Italian, Spanish and other supported mappings.
The actual text/audio available depends on your disc's files; this does not
download translations or promise dubbing in every language.

Language selection does not alter checksums, executables or save contents.
The normal configuration-backup mechanism preserves the previous settings.

## Adding another disc revision

The catalog is data-driven: `config/supported-dumps.json` and
`config/experimental-dumps.json` populate the launcher. Adding another language
choice usually does not require another disc entry. A different ISO or
executable revision does require validation:

1. Record the exact ISO size and SHA-256 from a lawfully obtained local dump.
2. Record size and SHA-256 for `default.xex`, `SpeechFacade_default.xex` and
   `XMediaFacade_default.xex`, together with title/media/version metadata.
3. Check code generation and all hard-coded runtime hooks against that revision.
   Matching the game's title alone is insufficient.
4. Test native startup, gameplay, saves and trainer behavior using private
   profiles. Initially mark the edition experimental and document what passed.

Do not add invented hashes, disable verification or redistribute the game
files. No additional regional revisions have been verified beyond the two
entries currently shipped with the port.

## Image location and frame pacing

The ready screen shows the installed edition and last successfully installed
ISO path. Setup stores that path only in private `.local/setup-state.json`.
Older installations can discover a single ISO in `.local/images`; multiple
images require a manual choice. Use **Choose another ISO** when moving the
image or changing editions. Images are never included in source exports.

Graphics settings also expose the existing render limit (0–240 fps; 0 is
uncapped). Try 60 fps and 1× resolution for more GPU headroom. This leaves
simulation timing and the presentation limit unchanged; it cannot eliminate
first-use shader compilation or storage stalls. Existing settings remain until
you save a change, with the normal configuration backup.

## PC driving controls

Before Play, choose **PC keyboard + mouse camera** or **PC keyboard + mouse
steering** under Driving controls. W/S accelerate and brake, A/D steer, Space
operates the handbrake, R rewinds, Q/E shift, and Esc pauses, using the default
in-game controller layout. F6 opens host settings. The preset is saved when you
press Play; **Keep current controls** preserves it on subsequent launches.
**Controller only** disables keyboard/mouse emulation. A configuration backup
preserves previous custom bindings. Console button prompts remain in the game.

If an older trainer build produced a malformed wristband setting, the Linux
launcher now backs up and repairs that specific TOML error before starting.
Other configuration syntax errors are reported explicitly. Language assets
still come from your own disc; selecting a language does not add missing audio.

Version 1.0 maps plain arrow keys to the controller D-pad for the main menu
and pause menu, Enter to confirm and Backspace to return. Shift+Arrows moves
the camera instead. Existing PC presets with the old default arrow bindings
are migrated at launch with a configuration backup; custom bindings are kept.

The corrected 1.0.0 also repairs configurations whose schema marker was omitted
by the old in-game settings writer. It restores the recorded schema from a
backup, or requests the full legacy migration when no marker can be recovered.
Changing language no longer requires deleting settings. English and Polish
remain console-language selections using the installed disc's assets.

## Original DLC packages

On the ready screen choose **Install original DLC…** and select your own
Xbox 360 LIVE/PIRS/CON package for Forza Horizon (title ID `4D5309C9`). Rebuild
an older installation first so it has `pinyon_shift_dlc_extract`. ZIP archives
and packages for other titles are rejected. No DLC is downloaded or bundled.

The importer extracts into temporary storage, verifies successful reads, keeps
the package's license metadata, and installs into:

```text
<state>/user/0000000000000000/4D5309C9/00000002/<package-id>/
```

Existing packages and saves are never overwritten. Close the game before
installing. The runtime enumerates this content on the next launch. The trainer
uses a separate `user-modded` profile; an already-created trainer profile does
not automatically receive additions to the normal profile.

CLI equivalents, from the source directory:

```bash
./tools/pinyon-shift-cli.sh dlc list --state-root /path/to/preview
./tools/pinyon-shift-cli.sh dlc install --state-root /path/to/preview --package /path/to/package
```

The extraction path has been tested with synthetic STFS content. Compatibility
of individual car packs and the Rally expansion still requires testing with
the owner's actual packages and their matching game revision/title update.
The built-in treasure-map option is separate from installing original DLC.
