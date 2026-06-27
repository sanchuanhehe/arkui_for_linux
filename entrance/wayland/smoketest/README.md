# Wayland + EGL 出图链路验证(Stage C 种子)

`wlegl_smoke.c`:最小 Wayland 客户端,验证 ArkUI-X Linux/Wayland 移植的核心净新增路径——
`wl_display` → `xdg_toplevel` → `wl_egl_window` → `EGLSurface` → GLESv2 渲染 → 上屏 + `glReadPixels` 落帧。

**已在本机(headless weston + Mesa llvmpipe)验证通过**(见 `verified_frame.png`:480×800,
深色底 + 青/品红/黄三条带,Y 方向正确)。关键结论:`eglCreateWindowSurface` 一次成功,
**没有 mac 港的 GL 共享组/白屏真凶**——印证评估"Linux 顺着系统走"。

这段是 `adapter/linux/entrance` 的 `WindowView` / `virtual_rs_window`(把 NSWindow/CAOpenGLLayer
换成 Wayland surface + EGL window surface)的实现种子。

## 编译运行
```sh
wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h
wayland-scanner private-code  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c
cc -o wlegl_smoke wlegl_smoke.c xdg-shell-protocol.c $(pkg-config --cflags --libs wayland-client wayland-egl egl glesv2)
weston --backend=headless --socket=wl-smoke --width=480 --height=800 &
WAYLAND_DISPLAY=wl-smoke ./wlegl_smoke
```
