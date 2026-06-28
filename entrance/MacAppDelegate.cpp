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

#include <cstdlib>
#include <string>

#include "StageApplication.h"
#include "StageViewController.h"
#include "WindowView.h"
#include "base/log/log.h"

namespace {
constexpr int32_t WINDOW_WIDTH = 480;
constexpr int32_t WINDOW_HEIGHT = 800;
// Mirrors the macOS MacAppDelegate.mm defaults.
const char* const BUNDLE_DIRECTORY = "arkui-x";
const char* const STAGE_INSTANCE_NAME = "com.example.helloworld:entry:EntryAbility";
} // namespace

MacAppDelegate::~MacAppDelegate()
{
    delete window_;
    window_ = nullptr;
}

void MacAppDelegate::ApplicationDidFinishLaunching()
{
    LOGI("MacAppDelegate::ApplicationDidFinishLaunching");

    // Stage D: when a real .abc bundle is present (ACE_STAGE_LAUNCH set), mirror the
    // macOS path — configModuleWithBundleDirectory + launchApplication, then a
    // StageViewController whose LoadView brings up the WindowView + RS surface node and
    // drives the ability lifecycle (AppMain DispatchOnCreate/Foreground) so the page
    // tree renders through the C2-b texture present pipeline. Gated so the default
    // (no bundle) still opens the verified bare window and never crashes on a missing
    // module.
    if (std::getenv("ACE_STAGE_LAUNCH") != nullptr) {
        LOGI("MacAppDelegate: stage launch (bundle=%{public}s)", BUNDLE_DIRECTORY);
        StageApplication::ConfigModuleWithBundleDirectory(BUNDLE_DIRECTORY);
        StageApplication::LaunchApplication();
        stageViewController_ = new StageViewController(STAGE_INSTANCE_NAME);
        stageViewController_->LoadView();
        return;
    }

    // Default (Stage C): bring up the on-screen Wayland window only (verified
    // wl_egl_window + EGLSurface path), no ability/page — RS shows the entrance clear
    // color / ACE_TEST_FRAME pattern.
    window_ = new WindowView();
    window_->NotifySurfaceChangedWithWidth(WINDOW_WIDTH, WINDOW_HEIGHT, 1.0f);
    window_->ShowOnView(nullptr);
    window_->StartBaseDisplayLink();
}
