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

// Linux/Wayland app delegate. Port of the macOS NSApplicationDelegate to a plain
// C++ class. The macOS delegate created an NSWindow whose contentViewController
// built the WindowView + createSurfaceNode + DispatchOnCreate; the Wayland
// version owns a WindowView (which builds its own wl_surface/xdg_toplevel/EGL
// surface). Driven by the Wayland main loop in main.cpp (was NSApplication run).

#ifndef FOUNDATION_ACE_ADAPTER_LINUX_ENTRANCE_APP_DELEGATE_H
#define FOUNDATION_ACE_ADAPTER_LINUX_ENTRANCE_APP_DELEGATE_H

class WindowView;
class StageViewController;

class MacAppDelegate {
public:
    MacAppDelegate() = default;
    ~MacAppDelegate();

    // Was -applicationDidFinishLaunching:. Configures the stage bundle (Stage C),
    // then brings up the on-screen Wayland window.
    void ApplicationDidFinishLaunching();

    // Was -applicationShouldTerminateAfterLastWindowClosed:.
    bool ApplicationShouldTerminateAfterLastWindowClosed() const { return true; }

    WindowView* GetWindow() const { return window_; }

private:
    WindowView* window_ = nullptr;
    StageViewController* stageViewController_ = nullptr;
};

#endif // FOUNDATION_ACE_ADAPTER_LINUX_ENTRANCE_APP_DELEGATE_H
