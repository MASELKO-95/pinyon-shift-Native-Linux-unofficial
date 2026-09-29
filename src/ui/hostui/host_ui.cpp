#include "ui/hostui/host_ui.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/kernel/xam/module.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/ui/presenter.h>
#include <rex/ui/ui_event.h>
#include <rex/ui/virtual_key.h>
#include <rex/ui/window.h>

#include "ui/hostui/fh1_archive.h"
#include "ui/hostui/xds_texture.h"

namespace pinyon_shift::hostui {
namespace {

using rex::ui::ImmediateTexture;
using rex::ui::ImmediateTextureFilter;
using rex::ui::VirtualKey;

// The title lays its UI out in 1280x720 with a 90 % safe area.
constexpr float kTitleWidth = 1280.0f;
constexpr float kTitleHeight = 720.0f;
constexpr float kSafeLeft = 64.0f;
constexpr float kSafeTop = 36.0f;
constexpr float kSafeRight = kTitleWidth - kSafeLeft;
constexpr float kSafeBottom = kTitleHeight - kSafeTop;

// Layout in title pixels, after the pause menu: a large display-font title,
// display-font rows with a slanted magenta brush behind the focused one, and
// a button help bar at the bottom.
constexpr float kTitleSize = 64.0f;
constexpr float kTitleBaseline = 150.0f;
constexpr float kRowSize = 40.0f;
constexpr float kRowPitch = 54.0f;
constexpr float kRowsTop = 196.0f;
constexpr float kRowsLeft = 128.0f;
constexpr float kValueRight = 1050.0f;
constexpr float kValueSize = 30.0f;
constexpr float kBadgeSize = 20.0f;
constexpr float kHelpSize = 26.0f;
constexpr float kHelpBaseline = kSafeBottom - 20.0f;
constexpr size_t kVisibleRows = 8;
// The panel inside DirtMasks/SelectionContainer.xds, with a little of its
// ragged border.
constexpr UvRect kSelectionMaskPanel = {0.21f, 0.12f, 0.79f, 0.87f};

constexpr uint32_t Rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a = 255) {
  return r | g << 8 | b << 16 | a << 24;
}
constexpr uint32_t kWhite = Rgba(255, 255, 255);
constexpr uint32_t kDimWhite = Rgba(255, 255, 255, 215);
constexpr uint32_t kDisabled = Rgba(255, 255, 255, 90);
constexpr uint32_t kMagenta = Rgba(230, 0, 126);
constexpr uint32_t kOrange = Rgba(255, 106, 19);
// Dark over the left where the title's own pause menu sits (it opens behind
// the host menu when the title sees system UI), lighter towards the right.
constexpr uint32_t kBackdropLeft = Rgba(8, 6, 12, 238);
constexpr uint32_t kBackdropRight = Rgba(8, 6, 12, 150);

// Fonts.zip members; the "ru" variants add Cyrillic to the same design.
constexpr const char* kFontMembers[][2] = {
    {"eru_vector_aa.dt", "e_vector_aa.dt"},  // heavy italic display (pause menu items)
    {"bru_vector_aa.dt", "b_vector_aa.dt"},  // condensed slab (help bar, values)
};

std::unique_ptr<ImmediateTexture> LoadTexture(rex::ui::ImmediateDrawer& drawer,
                                              const Fh1Archive& archive, const char* name,
                                              bool white) {
  auto data = archive.Read(name);
  auto image = data ? DecodeXds(*data) : std::nullopt;
  if (!image) {
    REXLOG_WARN("Host UI: could not load texture {}", name);
    return nullptr;
  }
  if (white) {
    // Masks carry their shape in alpha; the vertex colour tints them.
    for (size_t i = 0; i < image->rgba.size(); i += 4) {
      image->rgba[i] = image->rgba[i + 1] = image->rgba[i + 2] = 255;
    }
  }
  return drawer.CreateTexture(image->width, image->height, ImmediateTextureFilter::kLinear, false,
                              image->rgba.data());
}

}  // namespace

HostUi::HostUi(rex::ReXApp& app, rex::ui::Presenter& presenter, rex::ui::ImmediateDrawer& drawer,
               rex::ui::Window& window, rex::input::InputSystem* input_system,
               std::filesystem::path game_data_root)
    : app_(app),
      presenter_(presenter),
      drawer_(drawer),
      window_(window),
      input_system_(input_system),
      game_data_root_(std::move(game_data_root)) {}

HostUi::~HostUi() { Close(); }

bool HostUi::LoadAssets() {
  if (assets_loaded_ || assets_failed_) {
    return assets_loaded_;
  }
  const auto started = std::chrono::steady_clock::now();
  const auto ui_root = game_data_root_ / "media" / "ui";
  auto fonts = Fh1Archive::Open(ui_root / "Fonts.zip");
  if (!fonts) {
    REXLOG_ERROR("Host UI: cannot open {}", (ui_root / "Fonts.zip").string());
    assets_failed_ = true;
    return false;
  }
  for (size_t face = 0; face < size_t(Face::kCount); ++face) {
    for (const char* member : kFontMembers[face]) {
      if (auto data = fonts->Read(member)) {
        fonts_[face] = VectorFont::Parse(*data);
        if (fonts_[face]) {
          break;
        }
      }
    }
    if (!fonts_[face]) {
      REXLOG_ERROR("Host UI: cannot load vector font {}", kFontMembers[face][1]);
      assets_failed_ = true;
      return false;
    }
  }
  const uint8_t white[4] = {255, 255, 255, 255};
  white_ = drawer_.CreateTexture(1, 1, ImmediateTextureFilter::kNearest, false, white);
  if (auto textures = Fh1Archive::Open(ui_root / "textures" / "Horizon.zip")) {
    selection_mask_ = LoadTexture(drawer_, *textures, "DirtMasks/SelectionContainer.xds", true);
    button_a_ = LoadTexture(drawer_, *textures, "Controller/A_Button.xds", false);
    button_b_ = LoadTexture(drawer_, *textures, "Controller/B_Button.xds", false);
  } else {
    REXLOG_WARN("Host UI: cannot open Horizon.zip; drawing without textures");
  }
  assets_loaded_ = true;
  REXLOG_INFO("Host UI assets loaded in {} ms",
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - started)
                  .count());
  return true;
}

bool HostUi::Open(std::unique_ptr<MenuScreen> screen) {
  if (!screen || !LoadAssets()) {
    return false;
  }
  screens_.clear();
  screens_.push_back(std::move(screen));
  pad_.Reset();
  if (!registered_) {
    registered_ = true;
    SetGuestUiActive(true);
    presenter_.AddUIDrawerFromUIThread(this, kZOrder);
    window_.AddInputListener(this, kZOrder);
  }
  RequestPaint();
  return true;
}

void HostUi::Push(std::unique_ptr<MenuScreen> screen) {
  if (is_open() && screen) {
    screens_.push_back(std::move(screen));
    RequestPaint();
  }
}

void HostUi::Close() {
  if (applying_) {
    // A row's action is running; destroying its screen now would destroy
    // the running function. Apply closes once the action returns.
    close_pending_ = true;
    return;
  }
  screens_.clear();
  row_rects_.clear();
  if (!registered_) {
    return;
  }
  registered_ = false;
  presenter_.RemoveUIDrawerFromUIThread(this);
  window_.RemoveInputListener(this);
  SetGuestUiActive(false);
  RequestPaint();
}

void HostUi::SetGuestUiActive(bool active) {
  if (guest_ui_active_ == active) {
    return;
  }
  guest_ui_active_ = active;
  if (active) {
    app_.AcquireGuestInputCapture();
  } else {
    app_.ReleaseGuestInputCapture();
  }
  rex::kernel::xam::xeXamSetHostUIActive(active);
}

void HostUi::RequestPaint() { presenter_.RequestUIPaintFromUIThread(); }

void HostUi::Apply(NavCommand command) {
  if (!is_open()) {
    return;
  }
  if (command == NavCommand::kClose) {
    Close();
    return;
  }
  applying_ = true;
  const MenuScreen::Result result = screens_.back()->Handle(command);
  applying_ = false;
  if (close_pending_) {
    close_pending_ = false;
    Close();
    return;
  }
  if (result == MenuScreen::Result::kBack) {
    screens_.pop_back();
    if (screens_.empty()) {
      Close();
      return;
    }
  }
  RequestPaint();
}

void HostUi::PollPad() {
  if (!input_system_) {
    return;
  }
  rex::input::X_INPUT_STATE state = {};
  using rex::X_RESULT;  // X_ERROR_SUCCESS expands to an unqualified cast
  if (input_system_->GetHostPadState(0, &state) != X_ERROR_SUCCESS) {
    return;
  }
  const auto now = uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - start_time_)
                                .count());
  for (NavCommand command :
       pad_.Poll(uint16_t(state.gamepad.buttons), int16_t(state.gamepad.thumb_lx),
                 int16_t(state.gamepad.thumb_ly), now)) {
    Apply(command);
  }
}

HostUi::Canvas HostUi::ComputeCanvas(rex::ui::UIDrawContext& context) const {
  float x = 0.0f, y = 0.0f;
  float width = float(context.render_target_width());
  float height = float(context.render_target_height());
  if (auto rect = presenter_.GetPaintedGuestOutputRectFromUIThread()) {
    x = float(rect->x);
    y = float(rect->y);
    width = float(rect->width);
    height = float(rect->height);
  }
  // Fit the 16:9 title space into the guest image.
  Canvas canvas;
  canvas.scale = std::min(width / kTitleWidth, height / kTitleHeight);
  canvas.x = x + (width - kTitleWidth * canvas.scale) * 0.5f;
  canvas.y = y + (height - kTitleHeight * canvas.scale) * 0.5f;
  return canvas;
}

HostUi::Text& HostUi::TextFor(Face face, float title_pixels) {
  const int pixels = std::max(1, int(std::lround(title_pixels * canvas_.scale)));
  const std::pair<int, int> key(int(face), pixels);
  for (auto& [existing, text] : texts_) {
    if (existing == key) {
      return text;
    }
  }
  atlases_.push_back(std::make_unique<GlyphAtlas>(*fonts_[size_t(face)], float(pixels)));
  Text text;
  text.atlas = atlases_.back().get();
  texts_.emplace_back(key, std::move(text));
  return texts_.back().second;
}

void HostUi::PrepareText(Text& text, std::u32string_view string) {
  text.dirty |= text.atlas->Prepare(string);
  if (text.dirty) {
    // Batches drawn earlier this frame use the old texture; point them at the
    // replacement, which holds a superset of its glyphs.
    auto replacement = drawer_.CreateTexture(text.atlas->width(), text.atlas->height(),
                                             ImmediateTextureFilter::kLinear, false,
                                             text.atlas->rgba().data());
    if (text.texture) {
      for (Batch& batch : batches_) {
        if (batch.texture == text.texture.get()) {
          batch.texture = replacement.get();
        }
      }
    }
    text.texture = std::move(replacement);
    text.dirty = false;
  }
}

float HostUi::DrawText(Face face, float title_pixels, std::string_view string, float x,
                       float baseline, uint32_t color, float align) {
  Text& text = TextFor(face, title_pixels);
  const std::u32string decoded = DecodeUtf8(string);
  PrepareText(text, decoded);
  const GlyphAtlas& atlas = *text.atlas;
  const float width = atlas.Measure(decoded);
  // Snap the pen to whole pixels; the atlas samples at pixel centres.
  float pen_x = std::round(canvas_.x + x * canvas_.scale - width * align);
  const float pen_y = std::round(canvas_.y + baseline * canvas_.scale);
  const float inverse_width = 1.0f / float(atlas.width());
  const float inverse_height = 1.0f / float(atlas.height());
  for (char32_t code_point : decoded) {
    const GlyphAtlas::Entry* entry = atlas.Find(code_point);
    if (!entry) {
      continue;
    }
    if (entry->width && entry->height) {
      const float x0 = std::round(pen_x) + float(entry->left);
      const float y0 = pen_y + float(entry->top);
      if (batches_.empty() || batches_.back().texture != text.texture.get() ||
          batches_.back().vertices.size() > 60000) {
        batches_.push_back(Batch{.texture = text.texture.get()});
      }
      Batch& batch = batches_.back();
      const auto base = uint16_t(batch.vertices.size());
      const float u0 = float(entry->x) * inverse_width;
      const float v0 = float(entry->y) * inverse_height;
      const float u1 = float(entry->x + entry->width) * inverse_width;
      const float v1 = float(entry->y + entry->height) * inverse_height;
      const float x1 = x0 + float(entry->width);
      const float y1 = y0 + float(entry->height);
      batch.vertices.push_back({x0, y0, u0, v0, color});
      batch.vertices.push_back({x1, y0, u1, v0, color});
      batch.vertices.push_back({x1, y1, u1, v1, color});
      batch.vertices.push_back({x0, y1, u0, v1, color});
      for (uint16_t index : {0, 1, 2, 0, 2, 3}) {
        batch.indices.push_back(uint16_t(base + index));
      }
    }
    pen_x += entry->advance;
  }
  return width / canvas_.scale;
}

void HostUi::DrawRect(float x, float y, float width, float height, uint32_t color,
                      ImmediateTexture* texture, float skew, UvRect uv) {
  DrawGradient(x, y, width, height, color, color, texture, skew, uv);
}

void HostUi::DrawGradient(float x, float y, float width, float height, uint32_t left_color,
                          uint32_t right_color, ImmediateTexture* texture, float skew,
                          UvRect uv) {
  // Solid fills sample a white texel: the drawer's untextured path draws
  // nothing here.
  if (!texture) {
    texture = white_.get();
  }
  if (batches_.empty() || batches_.back().texture != texture ||
      batches_.back().vertices.size() > 60000) {
    batches_.push_back(Batch{.texture = texture});
  }
  Batch& batch = batches_.back();
  const float x0 = canvas_.x + x * canvas_.scale;
  const float y0 = canvas_.y + y * canvas_.scale;
  const float x1 = x0 + width * canvas_.scale;
  const float y1 = y0 + height * canvas_.scale;
  const float shift = skew * canvas_.scale;
  const auto base = uint16_t(batch.vertices.size());
  batch.vertices.push_back({x0 + shift, y0, uv.u0, uv.v0, left_color});
  batch.vertices.push_back({x1 + shift, y0, uv.u1, uv.v0, right_color});
  batch.vertices.push_back({x1, y1, uv.u1, uv.v1, right_color});
  batch.vertices.push_back({x0, y1, uv.u0, uv.v1, left_color});
  for (uint16_t index : {0, 1, 2, 0, 2, 3}) {
    batch.indices.push_back(uint16_t(base + index));
  }
}

void HostUi::Flush() {
  for (const Batch& batch : batches_) {
    if (batch.indices.empty()) {
      continue;
    }
    rex::ui::ImmediateDrawBatch draw_batch;
    draw_batch.vertices = batch.vertices.data();
    draw_batch.vertex_count = int(batch.vertices.size());
    draw_batch.indices = batch.indices.data();
    draw_batch.index_count = int(batch.indices.size());
    drawer_.BeginDrawBatch(draw_batch);
    rex::ui::ImmediateDraw draw;
    draw.count = int(batch.indices.size());
    draw.texture = batch.texture;
    drawer_.Draw(draw);
    drawer_.EndDrawBatch();
  }
  batches_.clear();
}

void HostUi::Draw(rex::ui::UIDrawContext& context) {
  if (!is_open()) {
    return;
  }
  PollPad();
  if (!is_open()) {
    return;
  }
  canvas_ = ComputeCanvas(context);
  if (canvas_.scale <= 0.0f) {
    return;
  }
  if (canvas_.scale != atlas_scale_) {
    // New output size: rasterize the fonts again at the new pixel sizes.
    batches_.clear();
    texts_.clear();
    atlases_.clear();
    atlas_scale_ = canvas_.scale;
  }
  const MenuScreen& screen = *screens_.back();

  drawer_.Begin(context, float(context.render_target_width()),
                float(context.render_target_height()));
  // Dim the title behind the menu across the whole guest image.
  if (auto rect = presenter_.GetPaintedGuestOutputRectFromUIThread()) {
    const Canvas saved = canvas_;
    canvas_ = Canvas{float(rect->x), float(rect->y), 1.0f};
    DrawGradient(0.0f, 0.0f, float(rect->width), float(rect->height), kBackdropLeft,
                 kBackdropRight);
    canvas_ = saved;
  } else {
    DrawGradient(0.0f, 0.0f, kTitleWidth, kTitleHeight, kBackdropLeft, kBackdropRight);
  }

  DrawText(Face::kDisplay, kTitleSize, screen.title(), kRowsLeft - 16.0f, kTitleBaseline, kWhite);

  const auto& rows = screen.rows();
  size_t first = 0;
  if (rows.size() > kVisibleRows && screen.focus() >= kVisibleRows / 2) {
    first = std::min(screen.focus() - kVisibleRows / 2, rows.size() - kVisibleRows);
  }
  row_rects_.assign(rows.size(), RowRect{0, 0, -1, -1});
  for (size_t i = first; i < rows.size() && i < first + kVisibleRows; ++i) {
    const MenuRow& row = rows[i];
    const bool focused = i == screen.focus();
    const bool enabled = row.is_enabled();
    const float top = kRowsTop + float(i - first) * kRowPitch;
    const float baseline = top + kRowPitch * 0.72f;
    const float row_right = row.value ? kValueRight + 40.0f : kRowsLeft + 560.0f;
    if (focused) {
      DrawRect(kRowsLeft - 36.0f, top + 2.0f, row_right - kRowsLeft + 72.0f, kRowPitch - 4.0f,
               kMagenta, selection_mask_.get(), 10.0f, kSelectionMaskPanel);
    }
    const uint32_t label_color = !enabled ? kDisabled : (focused ? kWhite : kDimWhite);
    const float label_width =
        DrawText(Face::kDisplay, kRowSize, row.label, kRowsLeft, baseline, label_color);
    if (row.restart_required) {
      DrawText(Face::kLabel, kBadgeSize, "RESTART", kRowsLeft + label_width + 16.0f,
               baseline - 12.0f, kOrange);
    }
    if (row.value) {
      const std::string value = row.value();
      const float value_baseline = baseline - 3.0f;
      const float value_width =
          DrawText(Face::kLabel, kValueSize, value, kValueRight, value_baseline, label_color, 1.0f);
      if (focused && row.adjust) {
        DrawText(Face::kLabel, kValueSize, "<", kValueRight - value_width - 16.0f, value_baseline,
                 kWhite, 1.0f);
        DrawText(Face::kLabel, kValueSize, ">", kValueRight + 16.0f, value_baseline, kWhite);
      }
    }
    row_rects_[i] = RowRect{canvas_.x + (kRowsLeft - 36.0f) * canvas_.scale,
                            canvas_.y + top * canvas_.scale,
                            canvas_.x + (row_right + 36.0f) * canvas_.scale,
                            canvas_.y + (top + kRowPitch) * canvas_.scale};
  }

  // Button help bar.
  float help_x = kRowsLeft - 16.0f;
  const auto help = [&](ImmediateTexture* icon, const char* label) {
    if (icon) {
      DrawRect(help_x, kHelpBaseline - 24.0f, 30.0f, 30.0f, kWhite, icon);
      help_x += 36.0f;
    }
    help_x += DrawText(Face::kLabel, kHelpSize, label, help_x, kHelpBaseline, kWhite) + 32.0f;
  };
  help(button_a_.get(), "SELECT");
  help(button_b_.get(), screens_.size() > 1 ? "BACK" : "RESUME");

  Flush();
  drawer_.End();
  // Keep painting while open so the pad is polled even when the title is
  // paused and presents nothing new.
  RequestPaint();
}

void HostUi::OnKeyDown(rex::ui::KeyEvent& e) {
  if (!is_open()) {
    return;
  }
  const VirtualKey key = e.virtual_key();
  if (key >= VirtualKey::kF1 && key <= VirtualKey::kF24) {
    return;  // keybinds stay live
  }
  e.set_handled(true);
  switch (key) {
    case VirtualKey::kUp:
    case VirtualKey::kNumpad8:
      Apply(NavCommand::kUp);
      break;
    case VirtualKey::kDown:
    case VirtualKey::kNumpad2:
      Apply(NavCommand::kDown);
      break;
    case VirtualKey::kLeft:
    case VirtualKey::kNumpad4:
      Apply(NavCommand::kLeft);
      break;
    case VirtualKey::kRight:
    case VirtualKey::kNumpad6:
      Apply(NavCommand::kRight);
      break;
    case VirtualKey::kReturn:
    case VirtualKey::kSpace:
      if (!e.prev_state()) {
        Apply(NavCommand::kAccept);
      }
      break;
    case VirtualKey::kEscape:
    case VirtualKey::kBack:
      if (!e.prev_state()) {
        Apply(NavCommand::kBack);
      }
      break;
    default:
      break;
  }
}

void HostUi::OnKeyUp(rex::ui::KeyEvent& e) {
  if (is_open() && !(e.virtual_key() >= VirtualKey::kF1 && e.virtual_key() <= VirtualKey::kF24)) {
    e.set_handled(true);
  }
}

void HostUi::OnKeyChar(rex::ui::KeyEvent& e) {
  if (is_open()) {
    e.set_handled(true);
  }
}

void HostUi::OnMouseMove(rex::ui::MouseEvent& e) {
  if (!is_open()) {
    return;
  }
  e.set_handled(true);
  for (size_t i = 0; i < row_rects_.size(); ++i) {
    const RowRect& rect = row_rects_[i];
    if (float(e.x()) >= rect.x0 && float(e.x()) < rect.x1 && float(e.y()) >= rect.y0 &&
        float(e.y()) < rect.y1) {
      if (screens_.back()->focus() != i && screens_.back()->SetFocus(i)) {
        RequestPaint();
      }
      return;
    }
  }
}

void HostUi::OnMouseDown(rex::ui::MouseEvent& e) {
  if (!is_open()) {
    return;
  }
  e.set_handled(true);
  if (e.button() == rex::ui::MouseEvent::Button::kRight) {
    Apply(NavCommand::kBack);
    return;
  }
  if (e.button() != rex::ui::MouseEvent::Button::kLeft) {
    return;
  }
  const size_t focus = screens_.back()->focus();
  if (focus >= row_rects_.size()) {
    return;
  }
  const RowRect& rect = row_rects_[focus];
  if (float(e.x()) < rect.x0 || float(e.x()) >= rect.x1 || float(e.y()) < rect.y0 ||
      float(e.y()) >= rect.y1) {
    return;
  }
  const MenuRow& row = screens_.back()->rows()[focus];
  if (row.value && row.adjust) {
    // Clicking a value row steps it: on the value's left part backwards,
    // anywhere else forwards.
    const float split = canvas_.x + (kValueRight - 60.0f) * canvas_.scale;
    Apply(float(e.x()) < split ? NavCommand::kLeft : NavCommand::kRight);
  } else {
    Apply(NavCommand::kAccept);
  }
}

void HostUi::OnMouseUp(rex::ui::MouseEvent& e) {
  if (is_open()) {
    e.set_handled(true);
  }
}

void HostUi::OnMouseWheel(rex::ui::MouseEvent& e) {
  if (!is_open()) {
    return;
  }
  e.set_handled(true);
  if (e.scroll_y() > 0) {
    Apply(NavCommand::kUp);
  } else if (e.scroll_y() < 0) {
    Apply(NavCommand::kDown);
  }
}

}  // namespace pinyon_shift::hostui
