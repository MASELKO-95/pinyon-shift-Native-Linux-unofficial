#pragma once

namespace rex::ui {
class Presenter;
}

namespace pinyon_shift::ui {

// Saves the current guest output (the title's image at the internal
// resolution, without host overlays) as a PNG under <state>/photos, named by
// UTC time, on a background thread. Logs photo.saved with the path.
void SavePhoto(rex::ui::Presenter* presenter);
// Waits (up to five seconds) for a photo in progress; call before the
// presenter is destroyed.
void WaitForPhoto();

}  // namespace pinyon_shift::ui
