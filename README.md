# arkui_for_linux

ArkUI-X 的 **Linux / Wayland 原生适配层**(`ace_engine` 的 `adapter/linux`),与
[`arkui_for_macos`](https://github.com/sanchuanhehe/arkui_for_macos)、上游
`arkui_for_android` / `arkui_for_ios` 同位:作为 `ace_engine` 的 `adapter/*` 独立子仓,
被克隆到 `foundation/arkui/ace_engine/adapter/linux/`。

目标:让标准 ArkUI(声明式 `.ets` + 方舟 `ets_runtime` + skia + RenderService 客户端侧)
在 **Wayland 原生窗口**里真实渲染——真 stage app(`ace` 可执行 + 完整 ability 生命周期 +
官方 `ace build bundle`),与 macOS 原生移植对等(M1 对等)。

## 计划目录结构(对照 adapter/macos、adapter/android)

| 目录 | 内容 |
|------|------|
| `entrance/` | `virtual_rs_window`(Wayland 窗口:`wl_display`/`xdg_shell`/`wl_egl_window`→`EGLSurface`)、main 入口、`wl_seat` 输入 |
| `osal/` | OS 抽象(log/file/...,Linux 原生,远少于 mac) |
| `stage/` | stage/ability 生命周期 + uicontent(`UIContentImpl`/`AceViewSG`) |
| `build/` | `BUILD.gn`:`ace_linux` 可执行 + Wayland/EGL/xkbcommon 链接 |

## 路线

路线 A:照 **Android 的 `ROSEN_ARKUI_X` 客户端侧 EGL 渲染**(`eglCreateWindowSurface`
直接渲染到 Wayland window surface,无 mac 的离屏 FBO/GL 共享组问题),把 Linux 从
`rosen_preview`(GLFW 预览)路由摘出来。

> 工程补丁集与完整复现见
> [`arkui-x-macos-native`](https://github.com/sanchuanhehe/arkui-x-macos-native)
> 仓的 `docs/linux-wayland-plan.md`。

## 许可
基于 OpenHarmony / ArkUI-X(Apache-2.0)二次开发,本仓以 **Apache-2.0** 发布。
