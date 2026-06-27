// Wayland + EGL smoke test for the ArkUI-X Linux/Wayland native port.
// Proves the core net-new path on this machine: wl_display -> xdg_toplevel ->
// wl_egl_window -> EGLSurface -> GLESv2 render -> on-screen + glReadPixels frame
// dump. This is the seed for adapter/linux/entrance (WindowView + virtual_rs_window).
//
// Renders a dark background + three colored bands (like the mac M1 test page),
// runs a few frames, dumps the EGL surface to a PPM, and exits.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include "xdg-shell-client-protocol.h"

static const int W = 480, H = 800;

static struct wl_display* display;
static struct wl_compositor* compositor;
static struct xdg_wm_base* wm_base;
static struct wl_surface* surface;
static struct xdg_surface* xdg_surface_obj;
static struct xdg_toplevel* xdg_toplevel_obj;
static struct wl_egl_window* egl_window;
static EGLDisplay egl_display;
static EGLContext egl_context;
static EGLSurface egl_surface;
static int configured = 0;

static void wm_base_ping(void* d, struct xdg_wm_base* b, uint32_t s) { xdg_wm_base_pong(b, s); }
static const struct xdg_wm_base_listener wm_base_listener = { .ping = wm_base_ping };

static void reg_global(void* d, struct wl_registry* r, uint32_t name, const char* iface, uint32_t ver)
{
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, xdg_wm_base_interface.name) == 0) {
        wm_base = wl_registry_bind(r, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
    }
}
static void reg_remove(void* d, struct wl_registry* r, uint32_t name) {}
static const struct wl_registry_listener reg_listener = { .global = reg_global, .global_remove = reg_remove };

static void xdg_surf_configure(void* d, struct xdg_surface* s, uint32_t serial)
{
    xdg_surface_ack_configure(s, serial);
    configured = 1;
}
static const struct xdg_surface_listener xdg_surf_listener = { .configure = xdg_surf_configure };

static void top_configure(void* d, struct xdg_toplevel* t, int32_t w, int32_t h, struct wl_array* st) {}
static void top_close(void* d, struct xdg_toplevel* t) {}
static const struct xdg_toplevel_listener top_listener = { .configure = top_configure, .close = top_close };

static void draw(void)
{
    glViewport(0, 0, W, H);
    glClearColor(0.12f, 0.12f, 0.14f, 1.0f); // dark bg
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    // three centered colored bands (cyan / magenta / yellow)
    struct { int y; float r, g, b; } bands[] = {
        { 560, 0.0f, 0.8f, 0.9f }, { 380, 0.9f, 0.2f, 0.6f }, { 200, 0.95f, 0.85f, 0.1f },
    };
    for (int i = 0; i < 3; i++) {
        glScissor(60, bands[i].y, W - 120, 120);
        glClearColor(bands[i].r, bands[i].g, bands[i].b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
}

static void dump_ppm(const char* path)
{
    unsigned char* px = malloc(W * H * 4);
    glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = H - 1; y >= 0; y--) { // GL origin bottom-left -> flip
        for (int x = 0; x < W; x++) {
            unsigned char* p = px + (y * W + x) * 4;
            fputc(p[0], f); fputc(p[1], f); fputc(p[2], f);
        }
    }
    fclose(f);
    free(px);
}

int main(void)
{
    display = wl_display_connect(NULL);
    if (!display) { fprintf(stderr, "no wl_display\n"); return 1; }
    struct wl_registry* reg = wl_display_get_registry(display);
    wl_registry_add_listener(reg, &reg_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !wm_base) { fprintf(stderr, "missing compositor/xdg_wm_base\n"); return 1; }

    surface = wl_compositor_create_surface(compositor);
    xdg_surface_obj = xdg_wm_base_get_xdg_surface(wm_base, surface);
    xdg_surface_add_listener(xdg_surface_obj, &xdg_surf_listener, NULL);
    xdg_toplevel_obj = xdg_surface_get_toplevel(xdg_surface_obj);
    xdg_toplevel_add_listener(xdg_toplevel_obj, &top_listener, NULL);
    xdg_toplevel_set_title(xdg_toplevel_obj, "ArkUI-X (Linux/Wayland)");
    wl_surface_commit(surface);
    while (!configured) wl_display_dispatch(display);

    egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    eglInitialize(egl_display, NULL, NULL);
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfg_attr[] = { EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE };
    EGLConfig cfg; EGLint n;
    eglChooseConfig(egl_display, cfg_attr, &cfg, 1, &n);
    EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    egl_context = eglCreateContext(egl_display, cfg, EGL_NO_CONTEXT, ctx_attr);
    egl_window = wl_egl_window_create(surface, W, H);
    egl_surface = eglCreateWindowSurface(egl_display, cfg, (EGLNativeWindowType)egl_window, NULL);
    if (egl_surface == EGL_NO_SURFACE) { fprintf(stderr, "eglCreateWindowSurface failed\n"); return 1; }
    eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context);

    printf("GL_RENDERER: %s\n", glGetString(GL_RENDERER));
    printf("GL_VERSION : %s\n", glGetString(GL_VERSION));

    for (int frame = 0; frame < 5; frame++) {
        draw();
        if (frame == 4) dump_ppm("/tmp/claude-0/-root-openharmony-arkui-x/318ed7f7-f7d0-48ed-9d8a-814b9e0cfa31/scratchpad/wlegl/frame.ppm");
        eglSwapBuffers(egl_display, egl_surface);
        wl_display_roundtrip(display);
    }
    printf("OK: rendered 5 frames, dumped frame.ppm\n");
    return 0;
}
