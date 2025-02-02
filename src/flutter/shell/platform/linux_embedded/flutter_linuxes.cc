// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/linux_embedded/public/flutter_linuxes.h"

#include <cassert>
#include <cstdlib>
#include <memory>
#include <vector>

#include "flutter/shell/platform/common/client_wrapper/include/flutter/plugin_registrar.h"
#include "flutter/shell/platform/common/incoming_message_dispatcher.h"
#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux_embedded/flutter_linuxes_engine.h"
#include "flutter/shell/platform/linux_embedded/flutter_linuxes_state.h"
#include "flutter/shell/platform/linux_embedded/flutter_linuxes_view.h"
#include "flutter/shell/platform/linux_embedded/window_binding_handler.h"

#if defined(DISPLAY_BACKEND_TYPE_DRM_GBM)
#include "flutter/shell/platform/linux_embedded/surface/context_egl.h"
#include "flutter/shell/platform/linux_embedded/surface/linuxes_surface_gl_drm.h"
#include "flutter/shell/platform/linux_embedded/window/linuxes_window_drm.h"
#include "flutter/shell/platform/linux_embedded/window/native_window_drm_gbm.h"
#elif defined(DISPLAY_BACKEND_TYPE_DRM_EGLSTREAM)
#include "flutter/shell/platform/linux_embedded/surface/context_egl_drm_eglstream.h"
#include "flutter/shell/platform/linux_embedded/surface/linuxes_surface_gl_drm.h"
#include "flutter/shell/platform/linux_embedded/window/linuxes_window_drm.h"
#include "flutter/shell/platform/linux_embedded/window/native_window_drm_eglstream.h"
#elif defined(DISPLAY_BACKEND_TYPE_X11)
#include "flutter/shell/platform/linux_embedded/window/linuxes_window_x11.h"
#else
#include "flutter/shell/platform/linux_embedded/window/linuxes_window_wayland.h"
#endif

static_assert(FLUTTER_ENGINE_VERSION == 1, "");

// Returns the engine corresponding to the given opaque API handle.
static flutter::FlutterLinuxesEngine* EngineFromHandle(
    FlutterDesktopEngineRef ref) {
  return reinterpret_cast<flutter::FlutterLinuxesEngine*>(ref);
}

// Returns the opaque API handle for the given engine instance.
static FlutterDesktopEngineRef HandleForEngine(
    flutter::FlutterLinuxesEngine* engine) {
  return reinterpret_cast<FlutterDesktopEngineRef>(engine);
}

// Returns the view corresponding to the given opaque API handle.
static flutter::FlutterLinuxesView* ViewFromHandle(FlutterDesktopViewRef ref) {
  return reinterpret_cast<flutter::FlutterLinuxesView*>(ref);
}

// Returns the opaque API handle for the given view instance.
static FlutterDesktopViewRef HandleForView(flutter::FlutterLinuxesView* view) {
  return reinterpret_cast<FlutterDesktopViewRef>(view);
}

// Returns the texture registrar corresponding to the given opaque API handle.
static flutter::FlutterLinuxesTextureRegistrar* TextureRegistrarFromHandle(
    FlutterDesktopTextureRegistrarRef ref) {
  return reinterpret_cast<flutter::FlutterLinuxesTextureRegistrar*>(ref);
}

// Returns the opaque API handle for the given texture registrar instance.
static FlutterDesktopTextureRegistrarRef HandleForTextureRegistrar(
    flutter::FlutterLinuxesTextureRegistrar* registrar) {
  return reinterpret_cast<FlutterDesktopTextureRegistrarRef>(registrar);
}

uint64_t FlutterDesktopEngineProcessMessages(FlutterDesktopEngineRef engine) {
  return static_cast<flutter::TaskRunner*>(
             EngineFromHandle(engine)->task_runner())
      ->ProcessTasks()
      .count();
}

FlutterDesktopViewControllerRef FlutterDesktopViewControllerCreate(
    const FlutterDesktopViewProperties& view_properties,
    FlutterDesktopEngineRef engine) {
  std::unique_ptr<flutter::WindowBindingHandler> window_wrapper =

#if defined(DISPLAY_BACKEND_TYPE_DRM_GBM)
      std::make_unique<flutter::LinuxesWindowDrm<
          flutter::NativeWindowDrmGbm,
          flutter::SurfaceGlDrm<flutter::ContextEgl>>>(
          view_properties.windw_display_mode, view_properties.width,
          view_properties.height, view_properties.show_cursor);
#elif defined(DISPLAY_BACKEND_TYPE_DRM_EGLSTREAM)
      std::make_unique<flutter::LinuxesWindowDrm<
          flutter::NativeWindowDrmEglstream,
          flutter::SurfaceGlDrm<flutter::ContextEglDrmEglstream>>>(
          view_properties.windw_display_mode, view_properties.width,
          view_properties.height, view_properties.show_cursor);
#elif defined(DISPLAY_BACKEND_TYPE_X11)
      std::make_unique<flutter::LinuxesWindowX11>(
          view_properties.windw_display_mode, view_properties.width,
          view_properties.height, view_properties.show_cursor);
#else
      std::make_unique<flutter::LinuxesWindowWayland>(
          view_properties.windw_display_mode, view_properties.width,
          view_properties.height, view_properties.show_cursor);
#endif

  auto state = std::make_unique<FlutterDesktopViewControllerState>();
  state->view =
      std::make_unique<flutter::FlutterLinuxesView>(std::move(window_wrapper));
  if (!state->view->CreateRenderSurface()) {
    return nullptr;
  }

  // Take ownership of the engine, starting it if necessary.
  state->view->SetEngine(
      std::unique_ptr<flutter::FlutterLinuxesEngine>(EngineFromHandle(engine)));
  if (!state->view->GetEngine()->running()) {
    if (!state->view->GetEngine()->RunWithEntrypoint(nullptr)) {
      return nullptr;
    }
  }

  // Must happen after engine is running.
  state->view->SendInitialBounds();
  return state.release();
}

void FlutterDesktopViewControllerDestroy(
    FlutterDesktopViewControllerRef controller) {
  delete controller;
}

FlutterDesktopEngineRef FlutterDesktopViewControllerGetEngine(
    FlutterDesktopViewControllerRef controller) {
  return HandleForEngine(controller->view->GetEngine());
}

FlutterDesktopViewRef FlutterDesktopViewControllerGetView(
    FlutterDesktopViewControllerRef controller) {
  return HandleForView(controller->view.get());
}

bool FlutterDesktopViewDispatchEvent(FlutterDesktopViewRef view) {
  return ViewFromHandle(view)->DispatchEvent();
}

FlutterDesktopEngineRef FlutterDesktopEngineCreate(
    const FlutterDesktopEngineProperties& engine_properties) {
  flutter::FlutterProjectBundle project(engine_properties);
  auto engine = std::make_unique<flutter::FlutterLinuxesEngine>(project);
  return HandleForEngine(engine.release());
}

bool FlutterDesktopEngineDestroy(FlutterDesktopEngineRef engine_ref) {
  flutter::FlutterLinuxesEngine* engine = EngineFromHandle(engine_ref);
  bool result = true;
  if (engine->running()) {
    result = engine->Stop();
  }
  delete engine;
  return result;
}

bool FlutterDesktopEngineRun(FlutterDesktopEngineRef engine,
                             const char* entry_point) {
  return EngineFromHandle(engine)->RunWithEntrypoint(entry_point);
}

void FlutterDesktopEngineReloadSystemFonts(FlutterDesktopEngineRef engine) {
  EngineFromHandle(engine)->ReloadSystemFonts();
}

FlutterDesktopPluginRegistrarRef FlutterDesktopEngineGetPluginRegistrar(
    FlutterDesktopEngineRef engine, const char* plugin_name) {
  // Currently, one registrar acts as the registrar for all plugins, so the
  // name is ignored. It is part of the API to reduce churn in the future when
  // aligning more closely with the Flutter registrar system.

  return EngineFromHandle(engine)->GetRegistrar();
}

FlutterDesktopMessengerRef FlutterDesktopEngineGetMessenger(
    FlutterDesktopEngineRef engine) {
  return EngineFromHandle(engine)->messenger();
}

FlutterDesktopTextureRegistrarRef FlutterDesktopEngineGetTextureRegistrar(
    FlutterDesktopEngineRef engine) {
  return HandleForTextureRegistrar(
      EngineFromHandle(engine)->texture_registrar());
}

FlutterDesktopViewRef FlutterDesktopPluginRegistrarGetView(
    FlutterDesktopPluginRegistrarRef registrar) {
  return HandleForView(registrar->engine->view());
}

// Implementations of common/cpp/ API methods.

FlutterDesktopMessengerRef FlutterDesktopPluginRegistrarGetMessenger(
    FlutterDesktopPluginRegistrarRef registrar) {
  return registrar->engine->messenger();
}

void FlutterDesktopPluginRegistrarSetDestructionHandler(
    FlutterDesktopPluginRegistrarRef registrar,
    FlutterDesktopOnPluginRegistrarDestroyed callback) {
  registrar->engine->SetPluginRegistrarDestructionCallback(callback);
}

bool FlutterDesktopMessengerSendWithReply(FlutterDesktopMessengerRef messenger,
                                          const char* channel,
                                          const uint8_t* message,
                                          const size_t message_size,
                                          const FlutterDesktopBinaryReply reply,
                                          void* user_data) {
  return messenger->engine->SendPlatformMessage(channel, message, message_size,
                                                reply, user_data);
}

bool FlutterDesktopMessengerSend(FlutterDesktopMessengerRef messenger,
                                 const char* channel, const uint8_t* message,
                                 const size_t message_size) {
  return FlutterDesktopMessengerSendWithReply(messenger, channel, message,
                                              message_size, nullptr, nullptr);
}

void FlutterDesktopMessengerSendResponse(
    FlutterDesktopMessengerRef messenger,
    const FlutterDesktopMessageResponseHandle* handle, const uint8_t* data,
    size_t data_length) {
  messenger->engine->SendPlatformMessageResponse(handle, data, data_length);
}

void FlutterDesktopMessengerSetCallback(FlutterDesktopMessengerRef messenger,
                                        const char* channel,
                                        FlutterDesktopMessageCallback callback,
                                        void* user_data) {
  messenger->engine->message_dispatcher()->SetMessageCallback(channel, callback,
                                                              user_data);
}

FlutterDesktopTextureRegistrarRef FlutterDesktopRegistrarGetTextureRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  return HandleForTextureRegistrar(registrar->engine->texture_registrar());
}

int64_t FlutterDesktopTextureRegistrarRegisterExternalTexture(
    FlutterDesktopTextureRegistrarRef texture_registrar,
    const FlutterDesktopTextureInfo* texture_info) {
  return TextureRegistrarFromHandle(texture_registrar)
      ->RegisterTexture(texture_info);
}

bool FlutterDesktopTextureRegistrarUnregisterExternalTexture(
    FlutterDesktopTextureRegistrarRef texture_registrar, int64_t texture_id) {
  return TextureRegistrarFromHandle(texture_registrar)
      ->UnregisterTexture(texture_id);
}

bool FlutterDesktopTextureRegistrarMarkExternalTextureFrameAvailable(
    FlutterDesktopTextureRegistrarRef texture_registrar, int64_t texture_id) {
  return TextureRegistrarFromHandle(texture_registrar)
      ->MarkTextureFrameAvailable(texture_id);
}
