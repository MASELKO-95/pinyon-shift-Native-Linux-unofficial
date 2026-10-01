#pragma once

// The on-screen controller for touch screens (AP-4.2): a steering stick on
// the left, throttle, brake and the face buttons on the right, and Back and
// Start at the top, drawn over the game by the host UI and read by the title
// as a synthetic pad on user 0 beside any real controller. It shows itself on
// the first touch and hides after a while without one, so a player with a
// controller never sees it.

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/ui/window_listener.h>

namespace pinyon_shift::ui {

class TouchPad final : public rex::ui::WindowInputListener {
 public:
  // One control drawn as a disc, in window pixels.
  struct Shape {
    float x = 0.0f, y = 0.0f, radius = 0.0f;
    std::string label;
    bool pressed = false;
    bool stick = false;
    float stick_x = 0.0f, stick_y = 0.0f;  // the knob, for the stick
  };

  static TouchPad& Get();

  // The synthetic pad the title reads; owned by the input system.
  std::unique_ptr<rex::input::InputDriver> CreateDriver();

  // Called when the overlay should be drawn again (state or visibility).
  void SetChangedCallback(std::function<void()> callback);
  // While the host menus are open they take the touches (as mouse clicks).
  void SetSuspended(bool suspended);

  void OnTouchEvent(rex::ui::TouchEvent& e) override;

  bool Visible() const;
  std::vector<Shape> Shapes(float width, float height) const;
  rex::input::X_INPUT_GAMEPAD State() const;

 private:
  enum class Control : uint8_t {
    kNone,
    kStick,
    kThrottle,
    kBrake,
    kA,
    kB,
    kX,
    kY,
    kLeftBumper,
    kRightBumper,
    kBack,
    kStart,
  };
  struct Finger {
    Control control = Control::kNone;
    float origin_x = 0.0f, origin_y = 0.0f;  // the stick's centre
    float x = 0.0f, y = 0.0f;
  };
  struct Layout {
    Control control;
    const char* label;
    float x, y;    // fraction of the window width and height
    float radius;  // fraction of the window height
  };

  static const std::vector<Layout>& Controls();
  Control Hit(float x, float y, float width, float height) const;
  void Changed();

  mutable std::mutex mutex_;
  std::map<uint32_t, Finger> fingers_;
  float width_ = 0.0f, height_ = 0.0f;
  bool suspended_ = false;
  std::chrono::steady_clock::time_point last_touch_{};
  std::function<void()> changed_;
};

}  // namespace pinyon_shift::ui
