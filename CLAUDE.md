# arkui_for_linux — ArkUI-X Linux/Wayland 原生适配层

本仓是 ArkUI-X 在 **Linux/Wayland** 上的原生适配层（`adapter/linux`），独立子仓
`github.com/sanchuanhehe/arkui_for_linux`，作为大仓 `OpenHarmony/foundation/arkui/ace_engine/adapter/linux`
克隆进去。技术走 **Route A**：native Wayland 窗口 + 客户端侧 EGL/GLES（照 Android `ROSEN_ARKUI_X`
客户端渲染），原生 fontconfig，把 Linux 从 `rosen_preview` 摘出——绕开 mac 港的 GL 共享组/CoreText 坑。

## 1. 仓结构

- `entrance/` — Wayland 窗口/输入/主循环层（**本仓主体**）。`WindowView.{h,cpp}`（wl_surface/xdg_toplevel/
  wl_egl_window→EGLSurface + RS 帧→GLES2 纹理上屏 + `RunEventLoop` 统一 epoll 事件循环）、`virtual_rs_window.cpp`
  （Window/RSUIDirector 驱动 + RS 渲染线程）、`MacAppDelegate.{h,cpp}`（app 启动，env `ACE_STAGE_LAUNCH` gate
  stage 启动 vs 裸窗口）、`main.cpp`（连 Wayland → ApplicationDidFinishLaunching → RunEventLoop）。
- `osal/` — 平台抽象（文件路径/动态模块/IME/accessibility/`linux_link_stubs.cpp` 集中链接桩，镜像
  `adapter/macos/osal/mac_link_stubs.cpp`）。
- `stage/` — ability/uicontent 层（`StageApplication`/`StageViewController`（`LoadView` 驱动 ability 生命周期）
  /`StageAssetManager`/`stage_asset_provider`/`ui_content_impl`）。
- `build/` — `ace_linux` 可执行体 + `ace_osal_linux`/`ace_linux_entrance`/`stage_linux_entrance`/
  `ace_uicontent_linux` source set 的 BUILD.gn。

## 2. 工作约束（必须遵守）

- **改动范围**：本仓只放 `adapter/linux` 内容。大仓其他仓（graphic_2d/appframework/ace_engine 框架/resmgr/
  image_framework/skia 等）的适配改动**落 `arkui-x-macos-native` 的 `patches/linux-*.patch`**，提交到
  `port/linux-wayland` 分支，**不直推 main**。两边一键可复现（`apply_patches.sh <src> linux` 克隆本仓 +
  应用 linux 补丁）。
- **提交**：本仓改动提交到 `main`；commit message 收尾 `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。
- **自治执行**：能自行判断的技术决策直接定并继续；只有不可逆/真正需要拍板的外部决策（建仓/可见性/破坏性）才问。
- **验证门**（过了才算完成）：gn gen 通过 / `libace_static_linux` 全编通 / `ace_linux` 0 undefined / 开窗截图 /
  .ets 页面渲染截图。每个阶段都要**本机真编、真跑、headless weston 截图验证**，不许只规划不落地。
- 用中文交流。

## 3. 平台/构建关键事实（避坑）

- **链 GCC-13 libstdc++（非 libc++）**：OHOS linux-aarch64 实际链 libstdc++；libc++ 内部用法（`std::optional::__get()`
  等）会炸，缺标准头（`<memory>/<cstdint>/<mutex>`…）要补。
- **`-Wl,-z,defs` 已对 arkui-x linux gate 掉**（`build/config/compiler/BUILD.gn`）：arkui-x 的 .so 故意留
  运行时解析符号（trace/panda/symbol-config），照 android/ios 运行时由主体 ace_linux 解析。
- **lld 默认 error-limit=20**：链接 undefined 逐次只报前 20 个=假象；用 `eval "$链接命令 -Wl,--error-limit=0"`
  从 `out/arkui-x` 重跑拿全量。
- **abc 版本**：运行时 libark_jsruntime 接受 **24.0.0.0**（mac 港 systemres abc 头 `18 00 00 00` 即此版本且能渲染）；
  es2abc 默认产 24.0.0.0，匹配。
- **bundle 记录名**：arkui-x 共享 runtime（`module_profile.cpp:760 package=moduleName`）要**模块相对** record
  `entry/ets/...`（无 bundleName 前缀）；ets2bundle 默认产 `<bundleName>/<moduleName>/...`，需配/patch 去前缀。
- 详细移植史/恢复点见 Claude 记忆 `arkui-x-linux-port-progress`。

## 4. 阶段（Definition of Done，按依赖推进）

A 骨架+gn gen ✅ → B `libace_static_linux` 全编（882→0）✅ → C `ace_linux` 0 undefined + 开窗截图 ✅ →
C2 RS→屏纹理管线（色带验证）✅ → 事件循环整合（ability 生命周期执行）✅ → **D 真 .ets 上屏（进行中，
卡 hvigor 正规构建 bundle）** → #9 输入 / #10 窗口管理 / #11 CI / #12 打包。

## 5. Skills

- `/format-changes` — 对本仓改动行跑 clang-format（aarch64 工具链）。改完 C++ 收尾调用。
- `/build-run-verify` — 编 `ace_linux` + headless weston 跑 + `ACE_SHOT_PPM` 截图验证的标准流程。每个阶段验证门用它。
