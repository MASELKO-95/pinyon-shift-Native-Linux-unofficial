#pragma once

#include <string_view>

// Kernel file-open observer: tracks whether the movie being played is a boot
// splash intro, which the opt-in opening-movie skip may complete early.
void PinyonShiftObserveGuestFileOpen(std::string_view guest_path);
