// Copyright 2021 Sony Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_EMBEDDED_WINDOW_LINUXES_WINDOW_DRM_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_EMBEDDED_WINDOW_LINUXES_WINDOW_DRM_H_

#include <fcntl.h>
#include <libinput.h>
#include <linux/input-event-codes.h>
#include <systemd/sd-event.h>
#include <unistd.h>

#include <memory>
#include <thread>

#include "flutter/shell/platform/linux_embedded/logger.h"
#include "flutter/shell/platform/linux_embedded/surface/linuxes_surface_gl_drm.h"
#include "flutter/shell/platform/linux_embedded/window/linuxes_window.h"
#include "flutter/shell/platform/linux_embedded/window/native_window_drm.h"
#include "flutter/shell/platform/linux_embedded/window_binding_handler.h"

namespace flutter {

namespace {
constexpr char kFlutterDrmDeviceEnvironmentKey[] = "FLUTTER_DRM_DEVICE";
constexpr char kDrmDeviceDefaultFilename[] = "/dev/dri/card0";
}  // namespace

template <typename W, typename S>
class LinuxesWindowDrm : public LinuxesWindow, public WindowBindingHandler {
 public:
  LinuxesWindowDrm(FlutterWindowMode window_mode, int32_t width, int32_t height,
                   bool show_cursor)
      : display_valid_(false), is_pending_cursor_add_event_(false) {
    window_mode_ = window_mode;
    current_width_ = width;
    current_height_ = height;
    show_cursor_ = show_cursor;

    auto udev = udev_new();
    if (!udev) {
      LINUXES_LOG(ERROR) << "Failed to create udev instance.";
      return;
    }
    libinput_ = libinput_udev_create_context(&kLibinputInterface, NULL, udev);
    if (!libinput_) {
      LINUXES_LOG(ERROR) << "Failed to create libinput instance.";
      udev_unref(udev);
      return;
    }
    udev_unref(udev);

    constexpr char kSeatId[] = "seat0";
    auto ret = libinput_udev_assign_seat(libinput_, kSeatId);
    if (ret != 0) {
      LINUXES_LOG(ERROR) << "Failed to assign udev seat to libinput instance.";
      return;
    }

    ret = sd_event_new(&libinput_event_loop_);
    if (ret < 0) {
      LINUXES_LOG(ERROR) << "Failed to create libinput event loop.";
      return;
    }
    ret =
        sd_event_add_io(libinput_event_loop_, NULL, libinput_get_fd(libinput_),
                        EPOLLIN | EPOLLRDHUP | EPOLLPRI, OnLibinputEvent, this);
    if (ret < 0) {
      LINUXES_LOG(ERROR) << "Failed to listen for user input.";
      libinput_event_loop_ = sd_event_unref(libinput_event_loop_);
      return;
    }
  }

  ~LinuxesWindowDrm() {
    if (libinput_event_loop_) {
      sd_event_unref(libinput_event_loop_);
    }
    libinput_unref(libinput_);
    display_valid_ = false;
  }

  // |LinuxesWindow|
  bool IsValid() const override {
    if (!display_valid_ || !native_window_ || !render_surface_ ||
        !native_window_->IsValid() || !render_surface_->IsValid()) {
      return false;
    }
    return true;
  }

  // |FlutterWindowBindingHandler|
  bool DispatchEvent() override {
    constexpr uint64_t kMaxWaitTime = 0;
    sd_event_run(libinput_event_loop_, kMaxWaitTime);
    return true;
  }

  // |FlutterWindowBindingHandler|
  bool CreateRenderSurface(int32_t width, int32_t height) override {
    auto device_filename = std::getenv(kFlutterDrmDeviceEnvironmentKey);
    if ((!device_filename) || (device_filename[0] == '\0')) {
      LINUXES_LOG(WARNING) << kFlutterDrmDeviceEnvironmentKey
                           << " is not set, use " << kDrmDeviceDefaultFilename;
      device_filename = const_cast<char*>(kDrmDeviceDefaultFilename);
    }
    native_window_ = std::make_unique<W>(device_filename);
    if (!native_window_->IsValid()) {
      LINUXES_LOG(ERROR) << "Failed to create the native window";
      return false;
    }
    display_valid_ = true;

    render_surface_ = native_window_->CreateRenderSurface();
    if (!render_surface_->SetNativeWindow(native_window_.get())) {
      return false;
    }
    render_surface_->SetNativeWindowResource(native_window_.get());

    if (window_mode_ == FlutterWindowMode::kFullscreen) {
      current_width_ = native_window_->Width();
      current_height_ = native_window_->Height();
      LINUXES_LOG(INFO) << "Display output resolution: " << current_width_
                        << "x" << current_height_;
      if (binding_handler_delegate_) {
        binding_handler_delegate_->OnWindowSizeChanged(current_width_,
                                                       current_height_);
      }
    } else {
      // todo: implement here.
      LINUXES_LOG(ERROR) << "Not supported specific surface size.";
    }

    if (is_pending_cursor_add_event_) {
      native_window_->ShowCursor(pointer_x_, pointer_y_);
      is_pending_cursor_add_event_ = false;
    }

    return true;
  }

  // |FlutterWindowBindingHandler|
  void DestroyRenderSurface() override {
    // destroy the main surface before destroying the client window on DRM.
    render_surface_ = nullptr;
    native_window_ = nullptr;
  }

  // |FlutterWindowBindingHandler|
  void SetView(WindowBindingHandlerDelegate* view) override {
    binding_handler_delegate_ = view;
  }

  // |FlutterWindowBindingHandler|
  LinuxesRenderSurfaceTarget* GetRenderSurfaceTarget() const override {
    return render_surface_.get();
  }

  // |FlutterWindowBindingHandler|
  double GetDpiScale() override { return current_scale_; }

  // |FlutterWindowBindingHandler|
  PhysicalWindowBounds GetPhysicalWindowBounds() override {
    return {GetCurrentWidth(), GetCurrentHeight()};
  }

  // |FlutterWindowBindingHandler|
  void UpdateFlutterCursor(const std::string& cursor_name) override {
    if (show_cursor_) {
      native_window_->UpdateCursor(cursor_name, pointer_x_, pointer_y_);
    }
  }

  // |FlutterWindowBindingHandler|
  void UpdateVirtualKeyboardStatus(const bool show) override {
    // currently not supported.
  }

  // |FlutterWindowBindingHandler|
  std::string GetClipboardData() override { return clipboard_data_; }

  // |FlutterWindowBindingHandler|
  void SetClipboardData(const std::string& data) override {
    clipboard_data_ = data;
  }

 protected:
  static constexpr libinput_interface kLibinputInterface = {
      .open_restricted = [](const char* path, int flags,
                            void* user_data) -> int {
        auto ret = open(path, flags | O_CLOEXEC);
        if (ret == -1) {
          LINUXES_LOG(ERROR)
              << "Failed to open " << path << ", error: " << strerror(errno);
        }
        return ret;
      },
      .close_restricted = [](int fd, void* user_data) -> void { close(fd); },
  };

  static int OnLibinputEvent(sd_event_source* source, int fd, uint32_t revents,
                             void* data) {
    auto self = reinterpret_cast<LinuxesWindowDrm*>(data);
    auto ret = libinput_dispatch(self->libinput_);
    if (ret < 0) {
      LINUXES_LOG(ERROR) << "Failed to dispatch libinput events.";
      return -ret;
    }

    auto previous_pointer_x = self->pointer_x_;
    auto previous_pointer_y = self->pointer_y_;

    while (libinput_next_event_type(self->libinput_) != LIBINPUT_EVENT_NONE) {
      auto event = libinput_get_event(self->libinput_);
      auto event_type = libinput_event_get_type(event);

      switch (event_type) {
        case LIBINPUT_EVENT_DEVICE_ADDED:
          self->OnDeviceAdded(event);
          break;
        case LIBINPUT_EVENT_DEVICE_REMOVED:
          self->OnDeviceRemoved(event);
          break;
        case LIBINPUT_EVENT_KEYBOARD_KEY:
          self->OnKeyEvent(event);
          break;
        case LIBINPUT_EVENT_POINTER_MOTION:
          self->OnPointerMotion(event);
          break;
        case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
          self->OnPointerMotionAbsolute(event);
          break;
        case LIBINPUT_EVENT_POINTER_BUTTON:
          self->OnPointerButton(event);
          break;
        case LIBINPUT_EVENT_POINTER_AXIS:
          self->OnPointerAxis(event);
          break;
        case LIBINPUT_EVENT_TOUCH_DOWN:
          self->OnTouchDown(event);
          break;
        case LIBINPUT_EVENT_TOUCH_UP:
          self->OnTouchUp(event);
          break;
        case LIBINPUT_EVENT_TOUCH_MOTION:
          self->OnTouchMotion(event);
          break;
        case LIBINPUT_EVENT_TOUCH_CANCEL:
          self->OnTouchCancel(event);
          break;
        case LIBINPUT_EVENT_TOUCH_FRAME:
          // do nothing.
          break;
        default:
          break;
      }
      libinput_event_destroy(event);
    }

    if (self->show_cursor_ && ((self->pointer_x_ != previous_pointer_x) ||
                               (self->pointer_y_ != previous_pointer_y))) {
      self->native_window_->MoveCursor(self->pointer_x_, self->pointer_y_);
    }

    return 0;
  }

  void OnDeviceAdded(libinput_event* event) {
    auto device = libinput_event_get_device(event);
    if ((show_cursor_) &&
        (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER))) {
      // When launching the application, either route will be used depending on
      // the timing.
      if (native_window_) {
        native_window_->ShowCursor(pointer_x_, pointer_y_);
      } else {
        is_pending_cursor_add_event_ = true;
      }
    }
  }

  void OnDeviceRemoved(libinput_event* event) {
    auto device = libinput_event_get_device(event);
    if (show_cursor_ &&
        (libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER))) {
      native_window_->DismissCursor();
    }
  }

  void OnKeyEvent(libinput_event* event) {
    if (binding_handler_delegate_) {
      auto key_event = libinput_event_get_keyboard_event(event);
      auto evdev_keycode =
          static_cast<uint16_t>(libinput_event_keyboard_get_key(key_event));
      auto key_state = libinput_event_keyboard_get_key_state(key_event);
      binding_handler_delegate_->OnKey(evdev_keycode,
                                       (key_state == LIBINPUT_KEY_STATE_PRESSED)
                                           ? FLUTTER_LINUXES_BUTTON_DOWN
                                           : FLUTTER_LINUXES_BUTTON_UP);
    }
  }

  void OnPointerMotion(libinput_event* event) {
    if (binding_handler_delegate_) {
      auto pointer_event = libinput_event_get_pointer_event(event);
      auto dx = libinput_event_pointer_get_dx(pointer_event);
      auto dy = libinput_event_pointer_get_dy(pointer_event);

      auto new_pointer_x = pointer_x_ + dx;
      new_pointer_x = std::max(0.0, new_pointer_x);
      new_pointer_x =
          std::min(static_cast<double>(current_width_ - 1), new_pointer_x);
      auto new_pointer_y = pointer_y_ + dy;
      new_pointer_y = std::max(0.0, new_pointer_y);
      new_pointer_y =
          std::min(static_cast<double>(current_height_ - 1), new_pointer_y);

      binding_handler_delegate_->OnPointerMove(new_pointer_x, new_pointer_y);
      pointer_x_ = new_pointer_x;
      pointer_y_ = new_pointer_y;
    }
  }

  void OnPointerMotionAbsolute(libinput_event* event) {
    if (binding_handler_delegate_) {
      auto pointer_event = libinput_event_get_pointer_event(event);
      auto x = libinput_event_pointer_get_absolute_x_transformed(
          pointer_event, current_width_);
      auto y = libinput_event_pointer_get_absolute_y_transformed(
          pointer_event, current_height_);

      binding_handler_delegate_->OnPointerMove(x, y);
      pointer_x_ = x;
      pointer_y_ = y;
    }
  }

  void OnPointerButton(libinput_event* event) {
    if (binding_handler_delegate_) {
      auto pointer_event = libinput_event_get_pointer_event(event);
      auto button = libinput_event_pointer_get_button(pointer_event);
      auto state = libinput_event_pointer_get_button_state(pointer_event);

      FlutterPointerMouseButtons flutter_button;
      switch (button) {
        case BTN_LEFT:
          flutter_button = kFlutterPointerButtonMousePrimary;
          break;
        case BTN_RIGHT:
          flutter_button = kFlutterPointerButtonMouseSecondary;
          break;
        case BTN_MIDDLE:
          flutter_button = kFlutterPointerButtonMouseMiddle;
          break;
        case BTN_BACK:
          flutter_button = kFlutterPointerButtonMouseBack;
          break;
        case BTN_FORWARD:
          flutter_button = kFlutterPointerButtonMouseForward;
          break;
        default:
          LINUXES_LOG(ERROR) << "Not expected button input: " << button;
          return;
      }

      if (state == LIBINPUT_BUTTON_STATE_PRESSED) {
        binding_handler_delegate_->OnPointerDown(pointer_x_, pointer_y_,
                                                 flutter_button);
      } else {
        binding_handler_delegate_->OnPointerUp(pointer_x_, pointer_y_,
                                               flutter_button);
      }
    }
  }

  void OnPointerAxis(libinput_event* event) {
    if (binding_handler_delegate_) {
      auto pointer_event = libinput_event_get_pointer_event(event);
      if (libinput_event_pointer_has_axis(
              pointer_event, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL)) {
        ProcessPointerAxis(pointer_event,
                           LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL);
      }
      if (libinput_event_pointer_has_axis(
              pointer_event, LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL)) {
        ProcessPointerAxis(pointer_event,
                           LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL);
      }
    }
  }

  void OnTouchDown(libinput_event* event) {
    if (binding_handler_delegate_) {
      auto touch_event = libinput_event_get_touch_event(event);
      auto time = libinput_event_touch_get_time(touch_event);
      auto slot = libinput_event_touch_get_seat_slot(touch_event);
      auto x =
          libinput_event_touch_get_x_transformed(touch_event, current_width_);
      auto y =
          libinput_event_touch_get_y_transformed(touch_event, current_height_);
      binding_handler_delegate_->OnTouchDown(time, slot, x, y);
    }
  }

  void OnTouchUp(libinput_event* event) {
    if (binding_handler_delegate_) {
      auto touch_event = libinput_event_get_touch_event(event);
      auto time = libinput_event_touch_get_time(touch_event);
      auto slot = libinput_event_touch_get_seat_slot(touch_event);
      binding_handler_delegate_->OnTouchUp(time, slot);
    }
  }

  void OnTouchMotion(libinput_event* event) {
    if (binding_handler_delegate_) {
      auto touch_event = libinput_event_get_touch_event(event);
      auto time = libinput_event_touch_get_time(touch_event);
      auto slot = libinput_event_touch_get_seat_slot(touch_event);
      auto x =
          libinput_event_touch_get_x_transformed(touch_event, current_width_);
      auto y =
          libinput_event_touch_get_y_transformed(touch_event, current_height_);
      binding_handler_delegate_->OnTouchMotion(time, slot, x, y);
    }
  }

  void OnTouchCancel(libinput_event* event) {
    if (binding_handler_delegate_) {
      binding_handler_delegate_->OnTouchCancel();
    }
  }

  void ProcessPointerAxis(libinput_event_pointer* pointer_event,
                          libinput_pointer_axis axis) {
    auto source = libinput_event_pointer_get_axis_source(pointer_event);
    double value = 0.0;
    switch (source) {
      case LIBINPUT_POINTER_AXIS_SOURCE_WHEEL:
        /* libinput < 0.8 sent wheel click events with value 10. Since 0.8
           the value is the angle of the click in degrees. To keep
           backwards-compat with existing clients, we just send multiples of
           the click count.
         */
        value = 10 * libinput_event_pointer_get_axis_value_discrete(
                         pointer_event, axis);
        break;
      case LIBINPUT_POINTER_AXIS_SOURCE_FINGER:
      case LIBINPUT_POINTER_AXIS_SOURCE_CONTINUOUS:
        value = libinput_event_pointer_get_axis_value(pointer_event, axis);
        break;
      default:
        LINUXES_LOG(ERROR) << "Not expected axis source: " << source;
        return;
    }

    constexpr int32_t kScrollOffsetMultiplier = 20;
    binding_handler_delegate_->OnScroll(
        pointer_x_, pointer_y_,
        axis == LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL ? 0 : value,
        axis == LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL ? value : 0,
        kScrollOffsetMultiplier);
  }

  // A pointer to a FlutterWindowsView that can be used to update engine
  // windowing and input state.
  WindowBindingHandlerDelegate* binding_handler_delegate_;

  std::unique_ptr<W> native_window_;
  std::unique_ptr<S> render_surface_;

  bool display_valid_;
  bool is_pending_cursor_add_event_;

  sd_event* libinput_event_loop_;
  libinput* libinput_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_EMBEDDED_WINDOW_LINUXES_WINDOW_DRM_H_
