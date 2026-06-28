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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

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

// Ability EventRunner integration (Stage D main loop): subscribe the Wayland fd to
// the runner's epoll IO waiter and drive the task queue from one blocking loop.
#include "event_handler.h"
#include "event_queue.h"
#include "event_runner.h"
#include "file_descriptor_listener.h"
#include "inner_event.h"

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

// FileDescriptorListener for the Wayland display fd. epoll (inside the ability
// EventRunner's EpollIoWaiter) reports the fd readable -> the queue posts a high
// priority task -> GetEvent returns it -> DistributeEvent runs OnReadable here.
// wl_display_dispatch then reads + dispatches the queued protocol events (xdg
// configure, input, and crucially the frame-callback done -> WindowView::Present).
// It does not busy-wait: it is only invoked when the fd is already readable, so the
// poll inside wl_display_dispatch returns at once. wl_display_flush pushes back out
// any client requests produced while dispatching (Present's surface commit + the
// re-armed wl_surface_frame callback).
namespace {
class WaylandFdListener : public OHOS::AppExecFwk::FileDescriptorListener {
public:
    explicit WaylandFdListener(wl_display* display) : display_(display) {}
    ~WaylandFdListener() override = default;

    void OnReadable(int32_t /*fileDescriptor*/) override
    {
        if (display_ == nullptr) {
            return;
        }
        wl_display_dispatch(display_);
        wl_display_flush(display_);
    }

private:
    wl_display* display_ = nullptr;
};
} // namespace

void WaylandContext::RunEventLoop()
{
    if (display_ == nullptr) {
        LOGE("WaylandContext::RunEventLoop: no Wayland display");
        return;
    }

    // The ability EventRunner created on this (main) thread by AppMain's ctor
    // (EventRunner::Current()); its EventQueue owns the EpollIoWaiter we hook the
    // Wayland fd into. Both are already live by the time the delegate has launched.
    auto runner = OHOS::AppExecFwk::EventRunner::Current();
    auto queue = OHOS::AppExecFwk::EventRunner::GetCurrentEventQueue();
    if (runner == nullptr || queue == nullptr) {
        LOGE("WaylandContext::RunEventLoop: no current EventRunner/queue");
        return;
    }

    // Subscribe the Wayland fd to the runner's epoll set. The listener's owner is
    // this handler (bound to the main runner), so the fd-ready high priority task
    // lands on the same queue that GetEvent below drains.
    auto handler = std::make_shared<OHOS::AppExecFwk::EventHandler>(runner);
    int32_t wlFd = wl_display_get_fd(display_);
    auto listener = std::make_shared<WaylandFdListener>(display_);
    auto ret = handler->AddFileDescriptorListener(
        wlFd, OHOS::AppExecFwk::FILE_DESCRIPTOR_INPUT_EVENT, listener);
    if (ret != 0) {
        LOGE("WaylandContext::RunEventLoop: AddFileDescriptorListener failed (%{public}d)",
            static_cast<int32_t>(ret));
    } else {
        LOGI("WaylandContext::RunEventLoop: Wayland fd %{public}d subscribed to runner epoll", wlFd);
    }

    // Flush the first surface commit (StartBaseDisplayLink's initial Present armed a
    // frame callback) so the compositor delivers the first frame-callback done event,
    // which wakes this loop through the Wayland fd and keeps Present vsync-paced.
    wl_display_flush(display_);

    LOGI("WaylandContext::RunEventLoop: entering unified epoll-blocking loop");
    // Single blocking loop. GetEvent's epoll_wait waits on BOTH the task wakeup fd
    // and the Wayland fd; it returns only when an ability task is ready or the
    // Wayland fd is readable -- zero spin, no sleep, no poll+retry.
    for (auto event = queue->GetEvent(); event; event = queue->GetEvent()) {
        auto owner = event->GetOwner();
        if (owner) {
            owner->DistributeEvent(event);
        }
        event.reset();
        // Push out any client requests the task just produced (e.g. a Present commit
        // + re-armed frame callback) so the compositor reacts and ticks the next vsync.
        wl_display_flush(display_);
    }
    LOGI("WaylandContext::RunEventLoop: loop exited");
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

// --- GLES2 texture-blit pipeline (Stage C2-b) ---
// Full-screen quad textured with the RS RGBA8888 frame. The RS bitmap is
// top-down (memory row 0 == top of image) while a GL texture's t=0 is its first
// uploaded row; the quad's UVs flip v so the top screen row samples row 0 and
// the image lands upright.
static const char* const TEX_VERT_SRC =
    "attribute vec2 a_pos;\n"
    "attribute vec2 a_uv;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = a_uv;\n"
    "    gl_Position = vec4(a_pos, 0.0, 1.0);\n"
    "}\n";
static const char* const TEX_FRAG_SRC =
    "precision mediump float;\n"
    "varying vec2 v_uv;\n"
    "uniform sampler2D u_tex;\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(u_tex, v_uv);\n"
    "}\n";

// Interleaved [pos.x, pos.y, uv.u, uv.v] * 6 verts (two triangles). uv.v is
// flipped vs pos.y to invert the top-down RS bitmap.
static const GLfloat TEX_QUAD[] = {
    -1.0f,  1.0f, 0.0f, 0.0f, // top-left
    -1.0f, -1.0f, 0.0f, 1.0f, // bottom-left
     1.0f, -1.0f, 1.0f, 1.0f, // bottom-right
    -1.0f,  1.0f, 0.0f, 0.0f, // top-left
     1.0f, -1.0f, 1.0f, 1.0f, // bottom-right
     1.0f,  1.0f, 1.0f, 0.0f, // top-right
};
constexpr GLuint TEX_ATTR_POS = 0;
constexpr GLuint TEX_ATTR_UV = 1;

static GLuint CompileShader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        LOGE("WindowView: glCreateShader failed (type=0x%x)", type);
        return 0;
    }
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLchar log[512] = { 0 };
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        LOGE("WindowView: shader compile failed: %{public}s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

WindowView* WindowView::instance_ = nullptr;

WindowView::WindowView()
{
    LOGI("WindowView init");
    // Single-window scenario: the latest constructed view is the present target
    // that OnRsFrame (a plain function pointer) reaches via this static pointer.
    instance_ = this;
}

WindowView::~WindowView()
{
    LOGI("WindowView dealloc");
    DestroyWaylandSurface();
    if (instance_ == this) {
        instance_ = nullptr;
    }
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
    instance_ = this; // this view now owns the live EGL surface -> present target.
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
        // Build the real Wayland surface chain. Stage C2-b: the surfaceNode's
        // additionalData is reinterpret_cast to Rosen's OnRenderFunc by the windows
        // platform group (rs_render_pipeline_client.cpp), so we MUST hand it a real
        // OnRenderFunc function pointer here -- NOT eglWindow_, which would be
        // called as a function on the first RS frame and crash. OnRsFrame's
        // signature matches OnRenderFunc exactly; RS invokes it on its render thread
        // and Present() blits the captured frame on the main thread.
        EnsureWaylandSurface();
        window->CreateSurfaceNode(reinterpret_cast<void*>(&WindowView::OnRsFrame));
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

// RS render-thread callback. Only copies the RGBA8888 page-tree bitmap into
// latestFrame_ under the mutex; the EGL context is main-thread-affine so all GL
// (texture upload + draw) happens later in Present().
bool WindowView::OnRsFrame(const void* addr, size_t size, int32_t w, int32_t h, uint64_t /*ts*/)
{
    WindowView* self = instance_;
    if (self == nullptr || addr == nullptr || size == 0 || w <= 0 || h <= 0) {
        return false;
    }
    const uint8_t* src = static_cast<const uint8_t*>(addr);
    {
        std::lock_guard<std::mutex> lock(self->frameMutex_);
        self->latestFrame_.assign(src, src + size);
        self->frameW_ = w;
        self->frameH_ = h;
        self->hasNewFrame_ = true;
    }
    // No explicit main-thread wakeup is needed: once StartBaseDisplayLink kicks the
    // vsync loop, Present() re-arms a wl_surface_frame callback every frame and will
    // pick this buffer up on the next tick.
    return true;
}

void WindowView::EnsureGlSetup()
{
    if (glSetup_) {
        return;
    }
    glSetup_ = true; // attempt only once; texProgram_==0 keeps Present's blit off on failure.

    GLuint vs = CompileShader(GL_VERTEX_SHADER, TEX_VERT_SRC);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, TEX_FRAG_SRC);
    if (vs == 0 || fs == 0) {
        if (vs != 0) {
            glDeleteShader(vs);
        }
        if (fs != 0) {
            glDeleteShader(fs);
        }
        LOGE("WindowView::EnsureGlSetup: shader compile failed; RS blit disabled");
        return;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    // Fix attribute locations so Present needs no glGetAttribLocation lookups.
    glBindAttribLocation(prog, TEX_ATTR_POS, "a_pos");
    glBindAttribLocation(prog, TEX_ATTR_UV, "a_uv");
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint linked = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLchar log[512] = { 0 };
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        LOGE("WindowView::EnsureGlSetup: program link failed: %{public}s", log);
        glDeleteProgram(prog);
        return;
    }
    texProgram_ = prog;

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(TEX_QUAD), TEX_QUAD, GL_STATIC_DRAW);

    glGenTextures(1, &texId_);
    glBindTexture(GL_TEXTURE_2D, texId_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    LOGI("WindowView::EnsureGlSetup: texture-blit pipeline ready (prog=%u tex=%u vbo=%u)",
        texProgram_, texId_, vbo_);
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

    // Compile the texture-blit program / quad / texture once the context is current.
    EnsureGlSetup();

    int32_t w = width_ > 0 ? width_ : DEFAULT_WIDTH;
    int32_t h = height_ > 0 ? height_ : DEFAULT_HEIGHT;

    // ACE_TEST_FRAME: feed a synthetic three-band RGBA pattern (cyan/magenta/yellow,
    // top to bottom) through the exact OnRsFrame -> latestFrame_ path, so the texture
    // upload + draw can be verified end-to-end before Stage D loads any .ets. Done
    // once; the band order matches the wlegl_smoke seed (top band == row 0 == cyan).
    static bool testFrameInjected = false;
    if (!testFrameInjected && getenv("ACE_TEST_FRAME") != nullptr && w > 0 && h > 0) {
        testFrameInjected = true;
        std::vector<uint8_t> pattern(static_cast<size_t>(w) * h * 4);
        for (int32_t y = 0; y < h; ++y) {
            uint8_t r = 0;
            uint8_t g = 0;
            uint8_t b = 0;
            if (y < h / 3) {
                r = 0; g = 200; b = 200; // top: cyan
            } else if (y < (h * 2) / 3) {
                r = 200; g = 0; b = 200; // middle: magenta
            } else {
                r = 200; g = 200; b = 0; // bottom: yellow
            }
            uint8_t* row = pattern.data() + static_cast<size_t>(y) * w * 4;
            for (int32_t x = 0; x < w; ++x) {
                row[x * 4 + 0] = r;
                row[x * 4 + 1] = g;
                row[x * 4 + 2] = b;
                row[x * 4 + 3] = 255;
            }
        }
        OnRsFrame(pattern.data(), pattern.size(), w, h, 0);
        LOGI("WindowView::Present: ACE_TEST_FRAME injected (%dx%d)", w, h);
    }

    // Background clear first; the textured quad (when there is content) covers the
    // whole viewport and overwrites it.
    glViewport(0, 0, w, h);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Pull the latest RS frame (if any) under the lock, then upload + draw outside
    // it. frameW_/frameH_ stay >0 once the first frame has arrived, so we keep
    // re-drawing the resident texture on ticks with no new frame.
    std::vector<uint8_t> frameCopy;
    bool newFrame = false;
    int32_t curW = 0;
    int32_t curH = 0;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        curW = frameW_;
        curH = frameH_;
        if (hasNewFrame_ && !latestFrame_.empty()) {
            frameCopy.swap(latestFrame_); // move out; the GPU texture retains content.
            hasNewFrame_ = false;
            newFrame = true;
        }
    }

    if (texProgram_ != 0 && curW > 0 && curH > 0) {
        glBindTexture(GL_TEXTURE_2D, texId_);
        if (newFrame &&
            frameCopy.size() >= static_cast<size_t>(curW) * curH * 4) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // RGBA rows are always 4-byte aligned.
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, curW, curH, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, frameCopy.data());
        }
        glUseProgram(texProgram_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        const GLsizei stride = 4 * sizeof(GLfloat);
        glEnableVertexAttribArray(TEX_ATTR_POS);
        glVertexAttribPointer(TEX_ATTR_POS, 2, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(0)));
        glEnableVertexAttribArray(TEX_ATTR_UV);
        glVertexAttribPointer(TEX_ATTR_UV, 2, GL_FLOAT, GL_FALSE, stride,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(2 * sizeof(GLfloat))));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texId_);
        glUniform1i(glGetUniformLocation(texProgram_, "u_tex"), 0);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(TEX_ATTR_POS);
        glDisableVertexAttribArray(TEX_ATTR_UV);
    }

    // [linux-port] Headless CI verification: ACE_SHOT_PPM=<path> dumps the rendered
    // back buffer once (glReadPixels is bottom-up, so write rows flipped) as a binary
    // PPM — no image lib needed; weston-screenshooter asserts on the headless output.
    //
    // The .ets page tree only reaches the screen several frames after launch (ability
    // onCreate -> loadContent -> RS render are async), so the very first Present is
    // just the clear color. Wait until a real RS frame has been uploaded + drawn
    // (texProgram_ && curW/curH > 0), then give the page a few vsync ticks to settle
    // before grabbing, so the capture holds the actual page, not clear color. If RS
    // never emits a frame, fall back to dumping the clear color after many ticks so the
    // run still produces an inspectable shot (and the empty page is diagnosable).
    static bool shotDone = false;
    static int32_t presentTicks = 0;
    static int32_t contentTicks = 0;
    ++presentTicks;
    const bool haveRealContent = (texProgram_ != 0 && curW > 0 && curH > 0);
    if (haveRealContent) {
        ++contentTicks;
    }
    constexpr int32_t SHOT_SETTLE_TICKS = 3;      // ticks of real RS content before grab
    constexpr int32_t SHOT_FALLBACK_TICKS = 600;  // give up waiting for RS, grab anyway
    const bool grabShot = haveRealContent ? (contentTicks >= SHOT_SETTLE_TICKS)
                                          : (presentTicks >= SHOT_FALLBACK_TICKS);
    const char* shotPath = getenv("ACE_SHOT_PPM");
    if (shotPath != nullptr && !shotDone && grabShot && w > 0 && h > 0) {
        shotDone = true;
        LOGI("WindowView::Present: capturing shot (%{public}s, presentTicks=%d contentTicks=%d)",
            haveRealContent ? "RS content" : "clear-color fallback", presentTicks, contentTicks);
        std::vector<unsigned char> rgba(static_cast<size_t>(w) * h * 4);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        FILE* fp = fopen(shotPath, "wb");
        if (fp != nullptr) {
            fprintf(fp, "P6\n%d %d\n255\n", w, h);
            std::vector<unsigned char> row(static_cast<size_t>(w) * 3);
            for (int32_t y = h - 1; y >= 0; --y) {
                const unsigned char* src = rgba.data() + static_cast<size_t>(y) * w * 4;
                for (int32_t x = 0; x < w; ++x) {
                    row[x * 3 + 0] = src[x * 4 + 0];
                    row[x * 3 + 1] = src[x * 4 + 1];
                    row[x * 3 + 2] = src[x * 4 + 2];
                }
                fwrite(row.data(), 1, row.size(), fp);
            }
            fclose(fp);
            LOGI("WindowView::Present: screenshot written to %{public}s (%dx%d)", shotPath, w, h);
        }
    }

    // Re-arm the frame callback before swapping for a vsync-paced loop.
    if (displayLinkStarted_ && wlSurface_ != nullptr) {
        frameCallback_ = wl_surface_frame(wlSurface_);
        wl_callback_add_listener(frameCallback_, &g_frameListener, this);
    }
    eglSwapBuffers(static_cast<EGLDisplay>(ctx.GetEglDisplay()),
        static_cast<EGLSurface>(eglSurface_));
}
