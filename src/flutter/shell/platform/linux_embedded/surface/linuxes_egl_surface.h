// Copyright 2021 Sony Corporation. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_SHELL_PLATFORM_LINUX_EMBEDDED_SURFACE_LINUXES_EGL_SURFACE_H_
#define FLUTTER_SHELL_PLATFORM_LINUX_EMBEDDED_SURFACE_LINUXES_EGL_SURFACE_H_

#include <EGL/egl.h>

#include "flutter/shell/platform/linux_embedded/logger.h"
#include "flutter/shell/platform/linux_embedded/surface/egl_utils.h"

#include "gl_extend_funcs.h"

namespace flutter {

class LinuxesEGLSurface {
 public:
  LinuxesEGLSurface(EGLSurface surface, EGLDisplay display, EGLContext context)
      : surface_(surface), display_(display), context_(context){};
  LinuxesEGLSurface(EGLSurface surface, EGLDisplay display, EGLContext context, EGLContext ex_context)
      : surface_(surface), display_(display), context_(context), ex_context_(ex_context){};

  ~LinuxesEGLSurface() {
    if (surface_ != EGL_NO_SURFACE) {
      if (eglDestroySurface(display_, surface_) != EGL_TRUE) {
        LINUXES_LOG(ERROR) << "Failed to destory surface";
      }
      surface_ = EGL_NO_SURFACE;
    }
  }

  bool IsValid() { return surface_ != EGL_NO_SURFACE; }

  bool MakeCurrent() {
    if (eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
      LINUXES_LOG(ERROR) << "Failed to make the EGL context current: "
                         << get_egl_error_cause();
      return false;
    }
    return true;
  }

  bool MakeCurrentEx() {
    if (eglMakeCurrent(display_, surface_, surface_, ex_context_) != EGL_TRUE) {
      LINUXES_LOG(ERROR) << "Failed to make the EGL context current: "
                         << get_egl_error_cause();
      return false;
    }
    return true;
  }

  bool RenderExternal() {
    PMETER_RESET_LAP ();
    PMETER_SET_LAP ();

    /* bind an external EGLContext */
    MakeCurrentEx();

    int win_w, win_h;
    eglQuerySurface (display_, surface_, EGL_WIDTH,  &win_w);
    eglQuerySurface (display_, surface_, EGL_HEIGHT, &win_h);

    /* initialize dbgstr, pmeter */
    static int s_ncount = 0;
    if (s_ncount == 0)
    {
      init_extend_funcs ();
      init_dbgstr (win_w, win_h);
      init_pmeter (win_w, win_h, win_h);
    }

    /* render dbgstr, pmeter */
    double interval_ms = pmeter_get_interval(0);
    char strbuf[128];
    int x = 100;
    int y = 0;
    float scale = 1.5f;
    float col_fg[] = {1.0, 1.0, 1.0f, 1.0f};
    float col_bg[] = {0.0, 0.0, 0.0f, 0.5f};

    sprintf (strbuf, "%dx%d", win_w, win_h);
    draw_dbgstr_ex (strbuf, x, y, scale, col_fg, col_bg); y += 22 * scale;

    sprintf (strbuf, "count   : %d", s_ncount);
    draw_dbgstr_ex (strbuf, x, y, scale, col_fg, col_bg); y += 22 * scale;

    sprintf (strbuf, "interval: %.1f[ms]", interval_ms);
    draw_dbgstr_ex (strbuf, x, y, scale, col_fg, col_bg); y += 22 * scale;

    sprintf (strbuf, "FPS     : %.1f", 1000.0f/interval_ms);
    draw_dbgstr_ex (strbuf, x, y, scale, col_fg, col_bg); y += 22 * scale;

    draw_pmeter (0, 0);
    s_ncount ++;

    /* bind the default EGLContext */
    MakeCurrent();

    return true;
  }

  bool SwapBuffers() {
    RenderExternal ();

    if (eglSwapBuffers(display_, surface_) != EGL_TRUE) {
      LINUXES_LOG(ERROR) << "Failed to swap the EGL buffer: "
                         << get_egl_error_cause();
      return false;
    }
    return true;
  }

 private:
  EGLDisplay display_;
  EGLSurface surface_;
  EGLContext context_;
  EGLContext ex_context_;
};

}  // namespace flutter

#endif  // FLUTTER_SHELL_PLATFORM_LINUX_EMBEDDED_SURFACE_LINUXES_EGL_SURFACE_H_