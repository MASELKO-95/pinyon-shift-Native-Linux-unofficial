#pragma once

#include <functional>
#include <string_view>

// Kernel file-open observer: tracks whether the movie being played is a boot
// splash intro, which the opt-in opening-movie skip may complete early.
void PinyonShiftObserveGuestFileOpen(std::string_view guest_path);

// Called on the guest thread when the player picks SETTINGS in the pause menu;
// the handler must hand the work to the UI thread and return at once.
// NP-4.4 Hor+: the factor the title's 16:9 main-view aspect is multiplied by
// (1 keeps it), so a wider window shows more of the world horizontally.
void PinyonShiftSetViewportAspectScale(float scale);

void PinyonShiftSetPauseSettingsHandler(std::function<void()> handler);
