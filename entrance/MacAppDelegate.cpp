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

// Linux/Wayland app delegate implementation. See MacAppDelegate.h.
//
// The macOS delegate did three things in applicationDidFinishLaunching:
//   1. [StageApplication configModuleWithBundleDirectory:] + launchApplication
//   2. build a StageViewController (which creates WindowView + surface node)
//   3. host it in an NSWindow and order it front
// On Wayland there is no NSWindow/NSViewController: the WindowView builds its own
// wl_surface + xdg_toplevel + wl_egl_window + EGLSurface. Step 1/2's AbilityRuntime
// stage launch is not wired on this path yet and is left as a Stage C stub.

#include "MacAppDelegate.h"

#include "WindowView.h"
#include "base/log/log.h"

namespace {
constexpr int32_t WINDOW_WIDTH = 480;
constexpr int32_t WINDOW_HEIGHT = 800;
} // namespace

MacAppDelegate::~MacAppDelegate()
{
    delete window_;
    window_ = nullptr;
}

void MacAppDelegate::ApplicationDidFinishLaunching()
{
    LOGI("MacAppDelegate::ApplicationDidFinishLaunching");

    // Stage C: configure the .abc / module bundle and start AbilityRuntime. The
    // macOS path called [StageApplication configModuleWithBundleDirectory:@"arkui-x"]
    // then [StageApplication launchApplication]; the Wayland stage host is a
    // follow-up. Once wired, Window::Create(context, windowView) replaces the bare
    // WindowView below so the RS/UIContent pipeline drives the surface.

    // Bring up the on-screen Wayland window (the verified wl_egl_window + EGLSurface
    // path). This mirrors what StageViewController did via createSurfaceNode.
    window_ = new WindowView();
    window_->NotifySurfaceChangedWithWidth(WINDOW_WIDTH, WINDOW_HEIGHT, 1.0f);
    window_->ShowOnView(nullptr);
    window_->StartBaseDisplayLink();
}
