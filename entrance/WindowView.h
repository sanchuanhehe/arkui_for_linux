/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Linux/Wayland WindowView. Port of the macOS NSView/CAOpenGLLayer WindowView to
// a plain C++ class backed by a Wayland surface + EGL window surface.
//
// The class name is kept exactly `WindowView` (global scope, no namespace)
// because virtual_rs_window.cpp references it by that name and virtual_rs_window.h
// forward-declares it. UIKit/AppKit -> Wayland: the NSView + CAEAGLLayer/
// CAOpenGLLayer drawable is replaced by a `wl_surface` + `xdg_toplevel` +
// `wl_egl_window` + `EGLSurface` (verified seed: wayland/smoketest/wlegl_smoke.c).
// Features with no Wayland equivalent (safe-area, status bar, orientation,
// screen brightness) are kept as no-op placeholders for source parity.

#ifndef FOUNDATION_ACE_ADAPTER_LINUX_ENTRANCE_WINDOW_VIEW_H
#define FOUNDATION_ACE_ADAPTER_LINUX_ENTRANCE_WINDOW_VIEW_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

// Forward-declare Wayland/EGL handles so this header stays light; the concrete
// wayland-client / wayland-egl / EGL headers are only pulled into WindowView.cpp.
struct wl_display;
struct wl_compositor;
struct wl_registry;
struct wl_surface;
struct wl_egl_window;
struct wl_seat;
struct wl_callback;
struct xdg_wm_base;
struct xdg_surface;
struct xdg_toplevel;

namespace OHOS::Rosen {
class Window;
}

// Replaces CGRect for touch hot-area plumbing (was CGRect[] on macOS).
struct WindowViewRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

// Synthetic / Wayland touch phase codes, matching the iOS UITouchPhase ordering
// the macOS port kept (0=Began, 1=Moved, 2=Stationary, 3=Ended, 4=Cancelled).
enum WindowViewTouchPhase : int32_t {
    WINDOW_VIEW_TOUCH_BEGAN = 0,
    WINDOW_VIEW_TOUCH_MOVED = 1,
    WINDOW_VIEW_TOUCH_STATIONARY = 2,
    WINDOW_VIEW_TOUCH_ENDED = 3,
    WINDOW_VIEW_TOUCH_CANCELLED = 4,
};

class WindowView;

// Wayland + EGL context shared by every WindowView and the app main loop.
// Replaces the AppKit NSApplication / NSScreen globals: main.cpp connects it and
// pumps wl_display_dispatch, while each WindowView borrows the compositor /
// xdg_wm_base / EGL context to build its own per-window surface chain.
class WaylandContext {
public:
    static WaylandContext& GetInstance();

    // wl_display_connect + registry bind (wl_compositor / xdg_wm_base / wl_seat).
    bool Connect();
    void Disconnect();
    // One blocking wl_display_dispatch; returns false once the display is gone.
    bool Dispatch();
    void Roundtrip();
    // Lazy eglInitialize + eglChooseConfig + a single shared EGLContext.
    bool EnsureEgl();

    wl_display* GetDisplay() const { return display_; }
    wl_compositor* GetCompositor() const { return compositor_; }
    xdg_wm_base* GetWmBase() const { return wmBase_; }
    void* GetEglDisplay() const { return eglDisplay_; }
    void* GetEglConfig() const { return eglConfig_; }
    void* GetEglContext() const { return eglContext_; }

    // Input routing: the seat handlers dispatch to the focused WindowView.
    void SetFocusView(WindowView* view) { focusView_ = view; }
    WindowView* GetFocusView() const { return focusView_; }

    // wl_registry global hook (public so the C trampoline can reach it).
    void HandleGlobal(wl_registry* registry, uint32_t name, const char* iface, uint32_t version);

private:
    WaylandContext() = default;
    ~WaylandContext();
    WaylandContext(const WaylandContext&) = delete;
    WaylandContext& operator=(const WaylandContext&) = delete;

    wl_display* display_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    xdg_wm_base* wmBase_ = nullptr;
    wl_seat* seat_ = nullptr;
    WindowView* focusView_ = nullptr;

    void* eglDisplay_ = nullptr; // EGLDisplay
    void* eglConfig_ = nullptr;  // EGLConfig
    void* eglContext_ = nullptr; // EGLContext
    bool eglReady_ = false;
};

class WindowView {
public:
    WindowView();
    ~WindowView();

    WindowView(const WindowView&) = delete;
    WindowView& operator=(const WindowView&) = delete;

    // Public fields replacing the ObjC @property accessors used by callers
    // (virtual_rs_window.cpp: windowView_->fullScreen = ..., ->zOrder, etc).
    bool focusable = true;
    bool isFocused = false;
    bool fullScreen = false;
    int64_t zOrder = 0;
    float brightness = 1.0f;

    // Window-delegate bridge (called from virtual_rs_window.cpp).
    void SetWindowDelegate(const std::shared_ptr<OHOS::Rosen::Window>& window);
    std::shared_ptr<OHOS::Rosen::Window> GetWindow();
    void CreateSurfaceNode();
    bool RequestFocus();
    void SetTouchHotAreas(const WindowViewRect* rects, int32_t size);
    bool ShowOnView(void* rootView);
    bool Hide();

    void NotifySurfaceChangedWithWidth(int32_t width, int32_t height, float density);
    void NotifySurfaceDestroyed();
    void NotifyForeground();
    void NotifyBackground();
    void NotifyHandleWillTerminate();
    void NotifyActiveChanged(bool isActive);
    void NotifyWindowDestroyed();
    void NotifySafeAreaChanged();
    void NotifyTraitCollectionDidChange(bool isSplitScreen);
    void NotifyApplicationForeground(bool isForeground);
    void NotifyFocusChanged(bool focus);

    void UpdateBrightness(bool isShow);
    bool ProcessBackPressed();
    void StartBaseDisplayLink();
    // Resizes the wl_egl_window (replaces the NSView.frame assignment).
    void Resize(int32_t width, int32_t height);

    // Synthetic touch injection (matches the macOS dispatchSyntheticTouchWithPhase:).
    bool DispatchSyntheticTouch(int32_t phase, double pixelX, double pixelY,
        int32_t pointerId, int64_t timeStamp);

    // Wayland input entry points, called by WaylandContext's seat handlers with
    // raw coordinates / keycodes (replaces the NSEvent source side).
    void OnPointerButton(double x, double y, int32_t button, bool pressed, int64_t timeStamp);
    void OnPointerMotion(double x, double y, int64_t timeStamp);
    void OnKey(int32_t keycode, bool pressed, int64_t timeStamp);

    // EGL present: make current, upload the latest RS frame as a texture, draw a
    // full-screen quad, swap buffers.
    void Present();

    // RS render-thread callback (Stage C2-b). Wired into the surfaceNode's
    // additionalData (cast to Rosen OnRenderFunc), invoked by RSSurfaceWindows::
    // FlushFrame on the RS render thread with the page tree read back as an
    // RGBA8888 (premultiplied) CPU bitmap of `w`*`h`*4 bytes. The signature must
    // match Rosen's `using OnRenderFunc = bool (*)(const void*, const size_t,
    // const int32_t, const int32_t, const uint64_t);` exactly. It only copies the
    // bytes into latestFrame_ under frameMutex_ (no GL here: the EGL context lives
    // on the main thread); Present() picks the frame up on the next vsync tick.
    static bool OnRsFrame(const void* addr, size_t size, int32_t w, int32_t h, uint64_t ts);

    // Wayland accessors used by the context / display-link plumbing.
    wl_surface* GetWlSurface() const { return wlSurface_; }
    bool IsConfigured() const { return configured_; }
    void SetConfigured(bool configured) { configured_ = configured; }

private:
    // Builds the wl_surface + xdg_toplevel + wl_egl_window + EGLSurface chain.
    void EnsureWaylandSurface();
    void DestroyWaylandSurface();
    void TouchOutside();
    void DispatchPointer(double x, double y, int32_t phase, int32_t pointerId, int64_t timeStamp);
    // One-time GLES2 setup (main thread, EGL context current): compiles the
    // texture-blit program, builds the full-screen quad VBO + the upload texture.
    void EnsureGlSetup();

    std::weak_ptr<OHOS::Rosen::Window> windowDelegate_;
    int32_t width_ = 0;
    int32_t height_ = 0;
    float density_ = 1.0f;
    bool needCreateSurfaceNode_ = false;
    bool needNotifySurfaceChanged_ = false;
    bool needNotifyForeground_ = false;
    bool needNotifyFocus_ = false;
    int32_t deviceId_ = 0;
    int32_t pointerId_ = 0;
    bool pointerPressed_ = false;
    float oldBrightness_ = -1.0f;
    std::vector<WindowViewRect> hotAreas_;

    // Wayland / EGL per-window objects.
    wl_surface* wlSurface_ = nullptr;
    xdg_surface* xdgSurface_ = nullptr;
    xdg_toplevel* xdgToplevel_ = nullptr;
    wl_egl_window* eglWindow_ = nullptr;
    wl_callback* frameCallback_ = nullptr;
    void* eglSurface_ = nullptr; // EGLSurface
    bool configured_ = false;
    bool displayLinkStarted_ = false;

    // ---- Stage C2-b: RS -> screen present (software raster texture upload) ----
    // OnRsFrame is a plain function pointer (Rosen OnRenderFunc cannot capture
    // `this`), so it reaches the active window through this static instance
    // pointer (single-window scenario).
    static WindowView* instance_;

    // Latest RS frame, produced on the RS render thread and consumed on the main
    // (present) thread. RGBA8888, frameW_*frameH_*4 bytes.
    std::mutex frameMutex_;
    std::vector<uint8_t> latestFrame_;
    int32_t frameW_ = 0;
    int32_t frameH_ = 0;
    bool hasNewFrame_ = false;

    // GLES2 resources for the texture blit (GLuint, kept as unsigned int so the
    // header needs no GL include).
    unsigned int texProgram_ = 0;
    unsigned int texId_ = 0;
    unsigned int vbo_ = 0;
    bool glSetup_ = false;
};

#endif // FOUNDATION_ACE_ADAPTER_LINUX_ENTRANCE_WINDOW_VIEW_H
