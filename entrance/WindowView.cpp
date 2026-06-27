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

// Linux/Wayland WindowView implementation. See WindowView.h for the porting
// overview. The macOS CAOpenGLLayer FBO->drawable blit is gone: on Wayland we
// render straight into an EGL window surface created over a wl_egl_window
// (verified path: wayland/smoketest/wlegl_smoke.c, eglCreateWindowSurface works
// first try, no GL-shared-group/white-screen issue).

#include "WindowView.h"

#include <cstring>
#include <limits>

#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "wayland/xdg-shell-client-protocol.h"

#include "hilog.h"
#include "base/log/log.h"
#include "base/utils/time_util.h"
#include "ace_pointer_data.h"
#include "ace_pointer_data_packet.h"
#include "virtual_rs_window.h"

namespace {
constexpr int32_t DEFAULT_WIDTH = 480;
constexpr int32_t DEFAULT_HEIGHT = 800;
constexpr int32_t CONFIGURE_ROUNDTRIP_LIMIT = 64;
// Left mouse button code reported by Wayland (linux/input-event-codes.h BTN_LEFT).
constexpr int32_t BTN_LEFT_CODE = 0x110;
} // namespace

// ============================================================================
// WaylandContext
// ============================================================================

// --- xdg_wm_base ping/pong ---
static void WmBasePing(void* /*data*/, struct xdg_wm_base* base, uint32_t serial)
{
    xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener g_wmBaseListener = { WmBasePing };

// --- wl_pointer ---
static void PointerEnter(void* data, struct wl_pointer* /*pointer*/, uint32_t /*serial*/,
    struct wl_surface* /*surface*/, wl_fixed_t sx, wl_fixed_t sy)
{
    auto* ctx = static_cast<WaylandContext*>(data);
    auto* view = ctx->GetFocusView();
    if (view != nullptr) {
        view->OnPointerMotion(wl_fixed_to_double(sx), wl_fixed_to_double(sy),
            static_cast<int64_t>(OHOS::Ace::GetSysTimestamp()));
    }
}
static void PointerLeave(void* /*data*/, struct wl_pointer* /*pointer*/, uint32_t /*serial*/,
    struct wl_surface* /*surface*/) {}
static void PointerMotion(void* data, struct wl_pointer* /*pointer*/, uint32_t time,
    wl_fixed_t sx, wl_fixed_t sy)
{
    auto* ctx = static_cast<WaylandContext*>(data);
    auto* view = ctx->GetFocusView();
    if (view != nullptr) {
        view->OnPointerMotion(wl_fixed_to_double(sx), wl_fixed_to_double(sy),
            static_cast<int64_t>(time) * 1000); // ms -> us
    }
}
static void PointerButton(void* data, struct wl_pointer* /*pointer*/, uint32_t /*serial*/,
    uint32_t time, uint32_t button, uint32_t state)
{
    auto* ctx = static_cast<WaylandContext*>(data);
    auto* view = ctx->GetFocusView();
    if (view != nullptr) {
        view->OnPointerButton(0.0, 0.0, static_cast<int32_t>(button),
            state == WL_POINTER_BUTTON_STATE_PRESSED, static_cast<int64_t>(time) * 1000);
    }
}
static void PointerAxis(void* /*data*/, struct wl_pointer* /*pointer*/, uint32_t /*time*/,
    uint32_t /*axis*/, wl_fixed_t /*value*/) {}
static const struct wl_pointer_listener g_pointerListener = {
    PointerEnter, PointerLeave, PointerMotion, PointerButton, PointerAxis,
};

// --- wl_keyboard (Stage C2: full xkb keymap translation) ---
static void KeyboardKeymap(void* /*data*/, struct wl_keyboard* /*kb*/, uint32_t /*format*/,
    int32_t /*fd*/, uint32_t /*size*/) {}
static void KeyboardEnter(void* /*data*/, struct wl_keyboard* /*kb*/, uint32_t /*serial*/,
    struct wl_surface* /*surface*/, struct wl_array* /*keys*/) {}
static void KeyboardLeave(void* /*data*/, struct wl_keyboard* /*kb*/, uint32_t /*serial*/,
    struct wl_surface* /*surface*/) {}
static void KeyboardKey(void* data, struct wl_keyboard* /*kb*/, uint32_t /*serial*/,
    uint32_t time, uint32_t key, uint32_t state)
{
    auto* ctx = static_cast<WaylandContext*>(data);
    auto* view = ctx->GetFocusView();
    if (view != nullptr) {
        // Stage C2: `key` is a Linux evdev keycode; ProcessKeyEvent expects the
        // HID-usage keycodes mapped by KeyCodeToAceKeyCode. Forward the raw code;
        // a proper evdev->HID table is a follow-up.
        view->OnKey(static_cast<int32_t>(key), state == WL_KEYBOARD_KEY_STATE_PRESSED,
            static_cast<int64_t>(time) * 1000);
    }
}
static void KeyboardModifiers(void* /*data*/, struct wl_keyboard* /*kb*/, uint32_t /*serial*/,
    uint32_t /*modsDepressed*/, uint32_t /*modsLatched*/, uint32_t /*modsLocked*/, uint32_t /*group*/) {}
static void KeyboardRepeatInfo(void* /*data*/, struct wl_keyboard* /*kb*/, int32_t /*rate*/,
    int32_t /*delay*/) {}
static const struct wl_keyboard_listener g_keyboardListener = {
    KeyboardKeymap, KeyboardEnter, KeyboardLeave, KeyboardKey, KeyboardModifiers, KeyboardRepeatInfo,
};

// --- wl_seat capabilities ---
static void SeatCapabilities(void* data, struct wl_seat* seat, uint32_t caps)
{
    auto* ctx = static_cast<WaylandContext*>(data);
    if (caps & WL_SEAT_CAPABILITY_POINTER) {
        struct wl_pointer* pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(pointer, &g_pointerListener, ctx);
    }
    if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
        struct wl_keyboard* kb = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(kb, &g_keyboardListener, ctx);
    }
    // Stage C2: WL_SEAT_CAPABILITY_TOUCH -> wl_touch for real multitouch.
}
static void SeatName(void* /*data*/, struct wl_seat* /*seat*/, const char* /*name*/) {}
static const struct wl_seat_listener g_seatListener = { SeatCapabilities, SeatName };

// --- wl_registry ---
static void RegistryGlobal(void* data, struct wl_registry* registry, uint32_t name,
    const char* iface, uint32_t version)
{
    static_cast<WaylandContext*>(data)->HandleGlobal(registry, name, iface, version);
}
static void RegistryGlobalRemove(void* /*data*/, struct wl_registry* /*registry*/, uint32_t /*name*/) {}
static const struct wl_registry_listener g_registryListener = { RegistryGlobal, RegistryGlobalRemove };

WaylandContext& WaylandContext::GetInstance()
{
    static WaylandContext instance;
    return instance;
}

WaylandContext::~WaylandContext()
{
    Disconnect();
}

void WaylandContext::HandleGlobal(
    wl_registry* registry, uint32_t name, const char* iface, uint32_t version)
{
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        compositor_ = static_cast<wl_compositor*>(
            wl_registry_bind(registry, name, &wl_compositor_interface, 4));
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        wmBase_ = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(wmBase_, &g_wmBaseListener, this);
    } else if (strcmp(iface, wl_seat_interface.name) == 0) {
        // Must bind on the SAME registry that advertised this global's name.
        seat_ = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
        wl_seat_add_listener(seat_, &g_seatListener, this);
    }
}

bool WaylandContext::Connect()
{
    if (display_ != nullptr) {
        return true;
    }
    display_ = wl_display_connect(nullptr);
    if (display_ == nullptr) {
        LOGE("WaylandContext::Connect: wl_display_connect failed");
        return false;
    }
    struct wl_registry* registry = wl_display_get_registry(display_);
    wl_registry_add_listener(registry, &g_registryListener, this);
    wl_display_roundtrip(display_);
    if (compositor_ == nullptr || wmBase_ == nullptr) {
        LOGE("WaylandContext::Connect: missing wl_compositor / xdg_wm_base");
        return false;
    }
    LOGI("WaylandContext::Connect: ok");
    return true;
}

void WaylandContext::Disconnect()
{
    if (display_ != nullptr) {
        wl_display_flush(display_);
        wl_display_disconnect(display_);
        display_ = nullptr;
    }
    compositor_ = nullptr;
    wmBase_ = nullptr;
    seat_ = nullptr;
    eglReady_ = false;
}

bool WaylandContext::Dispatch()
{
    if (display_ == nullptr) {
        return false;
    }
    return wl_display_dispatch(display_) != -1;
}

void WaylandContext::Roundtrip()
{
    if (display_ != nullptr) {
        wl_display_roundtrip(display_);
    }
}

bool WaylandContext::EnsureEgl()
{
    if (eglReady_) {
        return true;
    }
    if (display_ == nullptr) {
        return false;
    }
    EGLDisplay eglDisplay = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(display_));
    if (eglDisplay == EGL_NO_DISPLAY || !eglInitialize(eglDisplay, nullptr, nullptr)) {
        LOGE("WaylandContext::EnsureEgl: eglInitialize failed");
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint cfgAttr[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint num = 0;
    if (!eglChooseConfig(eglDisplay, cfgAttr, &config, 1, &num) || num < 1) {
        LOGE("WaylandContext::EnsureEgl: eglChooseConfig failed");
        return false;
    }
    const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext context = eglCreateContext(eglDisplay, config, EGL_NO_CONTEXT, ctxAttr);
    if (context == EGL_NO_CONTEXT) {
        LOGE("WaylandContext::EnsureEgl: eglCreateContext failed");
        return false;
    }
    eglDisplay_ = eglDisplay;
    eglConfig_ = config;
    eglContext_ = context;
    eglReady_ = true;
    LOGI("WaylandContext::EnsureEgl: ok");
    return true;
}

// ============================================================================
// WindowView
// ============================================================================

// --- per-window xdg_surface / xdg_toplevel listeners ---
static void XdgSurfaceConfigure(void* data, struct xdg_surface* surface, uint32_t serial)
{
    xdg_surface_ack_configure(surface, serial);
    static_cast<WindowView*>(data)->SetConfigured(true);
}
static const struct xdg_surface_listener g_xdgSurfaceListener = { XdgSurfaceConfigure };

static void XdgToplevelConfigure(void* /*data*/, struct xdg_toplevel* /*toplevel*/,
    int32_t /*width*/, int32_t /*height*/, struct wl_array* /*states*/) {}
static void XdgToplevelClose(void* data, struct xdg_toplevel* /*toplevel*/)
{
    static_cast<WindowView*>(data)->NotifyWindowDestroyed();
}
static const struct xdg_toplevel_listener g_xdgToplevelListener = {
    XdgToplevelConfigure, XdgToplevelClose,
};

// --- frame callback (replaces CVDisplayLink) ---
static void FrameDone(void* data, struct wl_callback* callback, uint32_t /*time*/);
static const struct wl_callback_listener g_frameListener = { FrameDone };

static void FrameDone(void* data, struct wl_callback* callback, uint32_t /*time*/)
{
    if (callback != nullptr) {
        wl_callback_destroy(callback);
    }
    static_cast<WindowView*>(data)->Present();
}

WindowView::WindowView()
{
    LOGI("WindowView init");
}

WindowView::~WindowView()
{
    LOGI("WindowView dealloc");
    DestroyWaylandSurface();
}

void WindowView::EnsureWaylandSurface()
{
    if (wlSurface_ != nullptr) {
        return;
    }
    auto& ctx = WaylandContext::GetInstance();
    if (ctx.GetCompositor() == nullptr || ctx.GetWmBase() == nullptr) {
        if (!ctx.Connect()) {
            LOGE("WindowView::EnsureWaylandSurface: Wayland not available");
            return;
        }
    }
    wlSurface_ = wl_compositor_create_surface(ctx.GetCompositor());
    xdgSurface_ = xdg_wm_base_get_xdg_surface(ctx.GetWmBase(), wlSurface_);
    xdg_surface_add_listener(xdgSurface_, &g_xdgSurfaceListener, this);
    xdgToplevel_ = xdg_surface_get_toplevel(xdgSurface_);
    xdg_toplevel_add_listener(xdgToplevel_, &g_xdgToplevelListener, this);
    xdg_toplevel_set_title(xdgToplevel_, "ArkUI-X (Linux/Wayland)");
    wl_surface_commit(wlSurface_);

    // Wait for the first configure before binding an EGL surface.
    int guard = 0;
    while (!configured_ && guard++ < CONFIGURE_ROUNDTRIP_LIMIT) {
        ctx.Dispatch();
    }

    if (!ctx.EnsureEgl()) {
        return;
    }
    int32_t w = width_ > 0 ? width_ : DEFAULT_WIDTH;
    int32_t h = height_ > 0 ? height_ : DEFAULT_HEIGHT;
    eglWindow_ = wl_egl_window_create(wlSurface_, w, h);
    EGLSurface surface = eglCreateWindowSurface(static_cast<EGLDisplay>(ctx.GetEglDisplay()),
        static_cast<EGLConfig>(ctx.GetEglConfig()),
        reinterpret_cast<EGLNativeWindowType>(eglWindow_), nullptr);
    if (surface == EGL_NO_SURFACE) {
        LOGE("WindowView::EnsureWaylandSurface: eglCreateWindowSurface failed");
        return;
    }
    eglSurface_ = surface;
    ctx.SetFocusView(this);
    LOGI("WindowView::EnsureWaylandSurface: surface %dx%d ready", w, h);
}

void WindowView::DestroyWaylandSurface()
{
    auto& ctx = WaylandContext::GetInstance();
    if (eglSurface_ != nullptr && ctx.GetEglDisplay() != nullptr) {
        eglDestroySurface(static_cast<EGLDisplay>(ctx.GetEglDisplay()),
            static_cast<EGLSurface>(eglSurface_));
        eglSurface_ = nullptr;
    }
    if (eglWindow_ != nullptr) {
        wl_egl_window_destroy(eglWindow_);
        eglWindow_ = nullptr;
    }
    if (xdgToplevel_ != nullptr) {
        xdg_toplevel_destroy(xdgToplevel_);
        xdgToplevel_ = nullptr;
    }
    if (xdgSurface_ != nullptr) {
        xdg_surface_destroy(xdgSurface_);
        xdgSurface_ = nullptr;
    }
    if (wlSurface_ != nullptr) {
        wl_surface_destroy(wlSurface_);
        wlSurface_ = nullptr;
    }
    if (ctx.GetFocusView() == this) {
        ctx.SetFocusView(nullptr);
    }
    configured_ = false;
}

// ---- Window delegate bridge ----

void WindowView::SetWindowDelegate(const std::shared_ptr<OHOS::Rosen::Window>& window)
{
    windowDelegate_ = window;
    if (needCreateSurfaceNode_) {
        needCreateSurfaceNode_ = false;
        CreateSurfaceNode();
    }
    if (needNotifySurfaceChanged_) {
        needNotifySurfaceChanged_ = false;
        NotifySurfaceChangedWithWidth(width_, height_, density_);
    }
    if (needNotifyForeground_) {
        needNotifyForeground_ = false;
        NotifyForeground();
    }
    if (needNotifyFocus_) {
        needNotifyFocus_ = false;
        NotifyFocusChanged(isFocused);
    }
}

std::shared_ptr<OHOS::Rosen::Window> WindowView::GetWindow()
{
    return windowDelegate_.lock();
}

void WindowView::CreateSurfaceNode()
{
    auto window = windowDelegate_.lock();
    if (window != nullptr) {
        // Build the real Wayland surface chain, then hand the EGL window handle to
        // Window/RS as the "layer" (the macOS code handed it the CALayer here).
        EnsureWaylandSurface();
        window->CreateSurfaceNode(eglWindow_);
    } else {
        needCreateSurfaceNode_ = true;
    }
}

void WindowView::NotifySurfaceChangedWithWidth(int32_t width, int32_t height, float density)
{
    width_ = width;
    height_ = height;
    density_ = density;
    auto window = windowDelegate_.lock();
    if (window != nullptr) {
        if (eglWindow_ != nullptr && width > 0 && height > 0) {
            wl_egl_window_resize(eglWindow_, width, height, 0, 0);
        }
        window->NotifySurfaceChanged(width, height, density);
    } else {
        needNotifySurfaceChanged_ = true;
    }
}

void WindowView::NotifySurfaceDestroyed()
{
    if (auto window = windowDelegate_.lock()) {
        window->NotifySurfaceDestroyed();
    }
}

void WindowView::NotifyWindowDestroyed()
{
    if (auto window = windowDelegate_.lock()) {
        window->Destroy();
    }
    DestroyWaylandSurface();
}

void WindowView::NotifySafeAreaChanged()
{
    if (auto window = windowDelegate_.lock()) {
        window->NotifySafeAreaChanged();
    }
}

void WindowView::NotifyForeground()
{
    if (auto window = windowDelegate_.lock()) {
        window->Foreground();
    } else {
        needNotifyForeground_ = true;
    }
}

void WindowView::NotifyBackground()
{
    if (auto window = windowDelegate_.lock()) {
        window->Background();
    }
}

void WindowView::NotifyActiveChanged(bool isActive)
{
    UpdateBrightness(isActive);
    if (auto window = windowDelegate_.lock()) {
        window->WindowActiveChanged(isActive);
    }
}

void WindowView::NotifyFocusChanged(bool focus)
{
    if (auto window = windowDelegate_.lock()) {
        window->WindowFocusChanged(focus);
    } else {
        needNotifyFocus_ = true;
    }
}

void WindowView::NotifyApplicationForeground(bool isForeground)
{
    if (auto window = windowDelegate_.lock()) {
        window->NotifyApplicationForeground(isForeground);
    }
}

void WindowView::NotifyTraitCollectionDidChange(bool isSplitScreen)
{
    if (auto window = windowDelegate_.lock()) {
        window->NotifyTraitCollectionDidChange(isSplitScreen);
    }
}

void WindowView::NotifyHandleWillTerminate()
{
    if (auto window = windowDelegate_.lock()) {
        window->NotifyWillTeminate();
    }
}

bool WindowView::ProcessBackPressed()
{
    if (auto window = windowDelegate_.lock()) {
        return window->ProcessBackPressed();
    }
    return false;
}

void WindowView::TouchOutside()
{
    if (auto window = windowDelegate_.lock()) {
        window->NotifyTouchOutside();
    }
}

// ---- Show / hide / focus ----

bool WindowView::RequestFocus()
{
    if (focusable) {
        WaylandContext::GetInstance().SetFocusView(this);
        return true;
    }
    return false;
}

void WindowView::SetTouchHotAreas(const WindowViewRect* rects, int32_t size)
{
    hotAreas_.clear();
    for (int32_t i = 0; i < size; ++i) {
        hotAreas_.push_back(rects[i]);
    }
}

bool WindowView::ShowOnView(void* /*rootView*/)
{
    // Wayland toplevels are mapped by committing their surface; there is no parent
    // NSView to attach to. Ensure the surface chain exists and commit it.
    EnsureWaylandSurface();
    if (wlSurface_ == nullptr) {
        return false;
    }
    wl_surface_commit(wlSurface_);
    WaylandContext::GetInstance().SetFocusView(this);
    return true;
}

bool WindowView::Hide()
{
    // Detach the buffer to unmap the surface (Wayland's equivalent of removing the
    // view from its superview).
    if (wlSurface_ != nullptr) {
        wl_surface_attach(wlSurface_, nullptr, 0, 0);
        wl_surface_commit(wlSurface_);
        return true;
    }
    return false;
}

void WindowView::Resize(int32_t width, int32_t height)
{
    width_ = width;
    height_ = height;
    if (eglWindow_ != nullptr && width > 0 && height > 0) {
        wl_egl_window_resize(eglWindow_, width, height, 0, 0);
    }
}

// ---- Brightness (Wayland: no per-screen brightness API) ----

void WindowView::UpdateBrightness(bool /*isShow*/)
{
    oldBrightness_ = brightness;
    // No public per-display brightness control under Wayland; store-only no-op.
}

// ---- Input ----

void WindowView::DispatchPointer(
    double x, double y, int32_t phase, int32_t pointerId, int64_t timeStamp)
{
    auto window = windowDelegate_.lock();
    if (window == nullptr) {
        return;
    }
    // Build a single-pointer AcePointerData packet and hand it to the window's
    // ProcessPointerEvent, which runs the shared mmi_event_convertor conversion.
    using OHOS::Ace::Platform::AcePointerData;
    using OHOS::Ace::Platform::AcePointerDataPacket;
    AcePointerData data;
    data.Clear();
    data.pointer_id = pointerId;
    data.device_id = deviceId_;
    data.finger_count = 1;
    data.time_stamp = timeStamp;
    data.window_x = x * density_;
    data.window_y = y * density_;
    data.display_x = data.window_x;
    data.display_y = data.window_y;
    data.tool_type = AcePointerData::ToolType::Mouse;
    data.actionPoint = true;
    switch (phase) {
        case WINDOW_VIEW_TOUCH_BEGAN:
            data.pointer_action = AcePointerData::PointerAction::kDowned;
            break;
        case WINDOW_VIEW_TOUCH_MOVED:
            data.pointer_action = AcePointerData::PointerAction::kMoved;
            break;
        case WINDOW_VIEW_TOUCH_ENDED:
            data.pointer_action = AcePointerData::PointerAction::kUped;
            break;
        case WINDOW_VIEW_TOUCH_CANCELLED:
            data.pointer_action = AcePointerData::PointerAction::kCanceled;
            break;
        default:
            data.pointer_action = AcePointerData::PointerAction::kMoved;
            break;
    }
    AcePointerDataPacket packet(1);
    packet.SetPointerData(0, data);
    window->ProcessPointerEvent(packet.data());
}

void WindowView::OnPointerButton(
    double x, double y, int32_t button, bool pressed, int64_t timeStamp)
{
    if (button != BTN_LEFT_CODE) {
        return; // Stage C2: secondary buttons / wheel.
    }
    pointerPressed_ = pressed;
    DispatchPointer(x, y, pressed ? WINDOW_VIEW_TOUCH_BEGAN : WINDOW_VIEW_TOUCH_ENDED,
        pointerId_, timeStamp);
}

void WindowView::OnPointerMotion(double x, double y, int64_t timeStamp)
{
    if (!pointerPressed_) {
        return; // only forward drags, matching the macOS mouseDragged path.
    }
    DispatchPointer(x, y, WINDOW_VIEW_TOUCH_MOVED, pointerId_, timeStamp);
}

void WindowView::OnKey(int32_t keycode, bool pressed, int64_t timeStamp)
{
    auto window = windowDelegate_.lock();
    if (window == nullptr) {
        return;
    }
    // KeyAction: 0 == UP, 1 == DOWN (Ace::KeyAction ordering).
    window->ProcessKeyEvent(keycode, pressed ? 1 : 0, 0, timeStamp, timeStamp, 0);
}

bool WindowView::DispatchSyntheticTouch(
    int32_t phase, double pixelX, double pixelY, int32_t pointerId, int64_t timeStamp)
{
    auto window = windowDelegate_.lock();
    if (window == nullptr) {
        return false;
    }
    using OHOS::Ace::Platform::AcePointerData;
    using OHOS::Ace::Platform::AcePointerDataPacket;
    AcePointerData data;
    data.Clear();
    data.pointer_id = pointerId;
    data.device_id = deviceId_;
    data.finger_count = 1;
    data.time_stamp = timeStamp;
    data.window_x = pixelX; // already physical pixels
    data.window_y = pixelY;
    data.display_x = pixelX;
    data.display_y = pixelY;
    data.tool_type = AcePointerData::ToolType::Touch;
    data.actionPoint = true;
    switch (phase) {
        case WINDOW_VIEW_TOUCH_BEGAN:
            data.pointer_action = AcePointerData::PointerAction::kDowned;
            break;
        case WINDOW_VIEW_TOUCH_MOVED:
            data.pointer_action = AcePointerData::PointerAction::kMoved;
            break;
        case WINDOW_VIEW_TOUCH_ENDED:
            data.pointer_action = AcePointerData::PointerAction::kUped;
            break;
        case WINDOW_VIEW_TOUCH_CANCELLED:
            data.pointer_action = AcePointerData::PointerAction::kCanceled;
            break;
        default:
            data.pointer_action = AcePointerData::PointerAction::kMoved;
            break;
    }
    AcePointerDataPacket packet(1);
    packet.SetPointerData(0, data);
    return window->ProcessSyntheticPointerEvent(packet.data());
}

// ---- Display link / present (CADisplayLink/CVDisplayLink -> wl frame callback) ----

void WindowView::StartBaseDisplayLink()
{
    if (displayLinkStarted_) {
        return;
    }
    displayLinkStarted_ = true;
    // Kick the first frame; FrameDone re-arms the callback each vsync.
    Present();
}

void WindowView::Present()
{
    auto& ctx = WaylandContext::GetInstance();
    if (eglSurface_ == nullptr || ctx.GetEglDisplay() == nullptr) {
        return;
    }
    eglMakeCurrent(static_cast<EGLDisplay>(ctx.GetEglDisplay()),
        static_cast<EGLSurface>(eglSurface_), static_cast<EGLSurface>(eglSurface_),
        static_cast<EGLContext>(ctx.GetEglContext()));

    // Stage C2: the RS render pipeline should blit the page tree's offscreen color
    // buffer into this EGL surface here (the macOS port did an FBO->drawable blit).
    // Until that wiring lands, clear to the configured background so the surface is
    // live and on-screen.
    int32_t w = width_ > 0 ? width_ : DEFAULT_WIDTH;
    int32_t h = height_ > 0 ? height_ : DEFAULT_HEIGHT;
    glViewport(0, 0, w, h);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Re-arm the frame callback before swapping for a vsync-paced loop.
    if (displayLinkStarted_ && wlSurface_ != nullptr) {
        frameCallback_ = wl_surface_frame(wlSurface_);
        wl_callback_add_listener(frameCallback_, &g_frameListener, this);
    }
    eglSwapBuffers(static_cast<EGLDisplay>(ctx.GetEglDisplay()),
        static_cast<EGLSurface>(eglSurface_));
}
