// Copyright 2021 Sony Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_EMBEDDED_WINDOW_NATIVE_WINDOW_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_EMBEDDED_WINDOW_NATIVE_WINDOW_H_

#include <EGL/egl.h>

namespace flutter {

class NativeWindow {
 public:
  NativeWindow() = default;
  virtual ~NativeWindow() = default;

  bool IsValid() const { return valid_; };

  EGLNativeWindowType Window() const { return window_; }

  int32_t Width() {
    if (!valid_) {
      return -1;
    }
    return width_;
  }

  int32_t Height() {
    if (!valid_) {
      return -1;
    }
    return height_;
  }

  virtual bool Resize(const size_t width, const size_t height) = 0;

 protected:
  EGLNativeWindowType window_;

  bool valid_ = false;
  int32_t width_;
  int32_t height_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_EMBEDDED_WINDOW_NATIVE_WINDOW_H_