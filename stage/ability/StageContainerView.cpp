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

// Stage C: Wayland surface. Linux stub for the macOS StageContainerView. All the
// NSView subview ordering / hit-testing / NSApplication notification observers
// belong to the Wayland surface layer and are left as TODOs.

#include "StageContainerView.h"

#include "base/log/log.h"

StageContainerView::StageContainerView()
{
    // Stage C: Wayland surface. The macOS init wired NSApplication active/hide/
    // unhide/terminate notification observers (TODO).
}

StageContainerView::~StageContainerView()
{
    LOGI("StageContainerView dealloc");
    // Stage C: Wayland surface. Notification observer teardown (TODO).
}

void StageContainerView::ShowWindow(void* window)
{
    // Stage C: Wayland surface. Subview z-order insertion + focus (TODO).
}

bool StageContainerView::RequestFocus(void* window)
{
    // Stage C: Wayland surface. Bring-to-front + focus (TODO).
    return false;
}

void StageContainerView::HiddenWindow(void* window)
{
    // Stage C: Wayland surface. Remove surface, restore main window (TODO).
    activeWindow_ = mainWindow_;
}

void StageContainerView::NotifyActiveChanged(bool actived)
{
    // Stage C: Wayland surface. Propagate focus change to surfaces (TODO).
}

void StageContainerView::NotifyForeground()
{
    // Stage C: Wayland surface. Propagate foreground to surfaces (TODO).
}

void StageContainerView::NotifyBackground()
{
    // Stage C: Wayland surface. Propagate background to surfaces (TODO).
}

void StageContainerView::SetMainWindow(void* mainWindow)
{
    mainWindow_ = mainWindow;
    activeWindow_ = mainWindow;
    // Stage C: Wayland surface. addSubview(mainWindow) (TODO).
}

void StageContainerView::SetActiveWindow(void* activeWindow)
{
    activeWindow_ = activeWindow;
    // Stage C: Wayland surface. focus bookkeeping (TODO).
}

void StageContainerView::SetNotifyDelegate(WindowViewDelegate* delegate)
{
    notifyDelegate_ = delegate;
}
