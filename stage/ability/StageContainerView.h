/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
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

// Linux/Wayland native port of the macOS Objective-C StageContainerView (an
// NSView managing the per-window WindowView z-order / focus). This is window/view
// glue: on Linux the windows are Wayland surfaces, not implemented in this layer.
// The class/method signatures are preserved so this compiles; the bodies are
// TODO ("Stage C: Wayland surface"). Window handles are opaque void*.

#ifndef FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_STAGECONTAINERVIEW_H
#define FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_STAGECONTAINERVIEW_H

// The macOS WindowViewDelegate (foreground/background/terminate hooks). Kept as a
// pure-virtual interface for source parity; the Wayland view layer implements it.
class WindowViewDelegate {
public:
    virtual ~WindowViewDelegate() = default;
    virtual void NotifyApplicationWillEnterForeground() {}
    virtual void NotifyApplicationDidEnterBackground() {}
    virtual void NotifyApplicationWillTerminateNotification() {}
};

class StageContainerView {
public:
    StageContainerView();
    ~StageContainerView();

    // window/mainWindow/activeWindow are opaque WindowView (Wayland surface) handles.
    void ShowWindow(void* window);
    bool RequestFocus(void* window);
    void HiddenWindow(void* window);
    // only invoked by StageViewController
    void NotifyActiveChanged(bool actived);
    // only invoked by StageViewController
    void NotifyForeground();
    // only invoked by StageViewController
    void NotifyBackground();
    void SetMainWindow(void* mainWindow);
    void SetActiveWindow(void* activeWindow);

    void SetNotifyDelegate(WindowViewDelegate* delegate);

private:
    void* activeWindow_ = nullptr;
    void* mainWindow_ = nullptr;
    WindowViewDelegate* notifyDelegate_ = nullptr;
};

#endif // FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_STAGECONTAINERVIEW_H
