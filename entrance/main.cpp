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

// Linux/Wayland native app entry. Replaces the macOS NSApplicationMain entry:
// there is no NSApplication / activation policy / Dock. Instead we connect to the
// Wayland display, hand off to MacAppDelegate (which brings up the WindowView),
// then run a wl_display_dispatch event loop (the analogue of [app run]).

#include "MacAppDelegate.h"
#include "WindowView.h"
#include "base/log/log.h"

int main(int argc, const char* argv[])
{
    (void)argc;
    (void)argv;

    // Connect the shared Wayland context (wl_display + registry: wl_compositor /
    // xdg_wm_base / wl_seat). Was [NSApplication sharedApplication].
    WaylandContext& ctx = WaylandContext::GetInstance();
    if (!ctx.Connect()) {
        LOGE("main: failed to connect to Wayland display");
        return 1;
    }

    MacAppDelegate delegate;
    delegate.ApplicationDidFinishLaunching();

    // Wayland event loop. Was [app run]. Dispatch returns false once the display
    // connection is gone (e.g. compositor exit), which ends the loop.
    while (ctx.Dispatch()) {
    }

    return 0;
}
