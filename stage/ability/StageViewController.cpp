/*
 * Copyright (c) 2023-2025 Huawei Device Co., Ltd.
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

// Stage C: Wayland surface. This file is the Linux stub for the macOS
// StageViewController. Only the portable instance-name/id bookkeeping is kept; the
// NSViewController lifecycle (loadView / viewDidLoad / viewWillAppear ...), the
// WindowView surface creation, and the AppMain DispatchOnCreate/Foreground/Destroy
// calls that the original made from those lifecycle hooks belong to the Wayland
// surface layer and are left as TODOs.

#include "StageViewController.h"

#include "InstanceIdGenerator.h"
#include "StageAssetManager.h"
#include "WindowView.h"
#include "app_main.h"
#include "base/log/log.h"
#include "version_printer.h"
#include "window_view_adapter.h"

namespace {
std::vector<std::string> SplitString(const std::string& str, char sep)
{
    std::vector<std::string> parts;
    std::string token;
    for (char ch : str) {
        if (ch == sep) {
            parts.emplace_back(token);
            token.clear();
        } else {
            token.push_back(ch);
        }
    }
    parts.emplace_back(token);
    return parts;
}

// Mirrors the macOS -ExistDir: a module directory is "present" when any scanned
// asset path contains "/<filePath>".
bool ExistModuleDir(const std::string& filePath)
{
    std::vector<std::string> assetList = StageAssetManager::GetInstance().GetAssetAllFilePathList();
    for (const auto& assetPath : assetList) {
        if (assetPath.find("/" + filePath) != std::string::npos) {
            return true;
        }
    }
    return false;
}
} // namespace

StageViewController::StageViewController(const std::string& instanceName)
{
    instanceId_ = InstanceIdGenerator::GetAndIncrement();
    instanceName_ = instanceName + ":" + std::to_string(instanceId_);
    LOGI("StageVC init, instanceName is : %{public}s", instanceName_.c_str());
    std::vector<std::string> nameArray = SplitString(instanceName_, ':');
    if (nameArray.size() >= 3) {
        std::string bundleName = nameArray[0];
        std::string moduleName = nameArray[1];
        std::string moduleNamePath = bundleName + "." + moduleName;
        if (ExistModuleDir(moduleNamePath) && nameArray.size() >= 4) {
            moduleName = moduleNamePath;
            instanceName_ = nameArray[0] + ":" + moduleName + ":" + nameArray[2] + ":" + nameArray[3];
        }
    }
    cInstanceName_ = instanceName_;
    homeIndicatorHidden = false;
    OHOS::Ace::Platform::VersionPrinter::printVersion();
    // Stage C: Wayland surface. Surface creation + AppMain::DispatchOnCreate /
    // DispatchOnForeground happen once the Wayland surface is attached (TODO).
}

StageViewController::~StageViewController()
{
    LOGI("StageVC dealloc instanceName: %{public}s", instanceName_.c_str());
    if (!cInstanceName_.empty()) {
        OHOS::AbilityRuntime::Platform::AppMain::GetInstance()->DispatchOnDestroy(cInstanceName_);
    }
}

// Stage D: mirrors the macOS StageViewController loadView/viewDidLoad. Brings up the
// Wayland WindowView (surface + RS surface node wired to OnRsFrame), registers it with
// the WindowViewAdapter under this instance name, then drives the ability lifecycle
// (AppMain DispatchOnCreate loads the module .abc + builds UIContent; DispatchOn
// Foreground renders the page tree). The page-tree pixels then flow RS -> OnRsFrame ->
// the C2-b texture present pipeline -> the wl_egl_window EGL surface.
void StageViewController::LoadView()
{
    LOGI("StageVC LoadView instanceName: %{public}s", cInstanceName_.c_str());
    auto* windowView = new WindowView();
    windowView_ = windowView;
    windowView->NotifySurfaceChangedWithWidth(480, 800, 1.0f);
    OHOS::AbilityRuntime::Platform::WindowViewAdapter::GetInstance()->AddWindowView(cInstanceName_, windowView_);
    // ShowOnView creates the wl_surface/wl_egl_window/EGLSurface first; only then start
    // the display-link present loop (Present early-returns + never re-arms the frame
    // callback if wlSurface_ is still null), matching the bare-window path order.
    windowView->ShowOnView(nullptr);
    windowView->CreateSurfaceNode();
    windowView->StartBaseDisplayLink();

    auto appMain = OHOS::AbilityRuntime::Platform::AppMain::GetInstance();
    appMain->DispatchOnCreate(cInstanceName_, "");
    appMain->DispatchOnForeground(cInstanceName_);
}

const std::string& StageViewController::GetInstanceName() const
{
    return instanceName_;
}

int32_t StageViewController::GetInstanceId() const
{
    return instanceId_;
}

bool StageViewController::ProcessBackPress()
{
    // Stage C: Wayland surface. Delegates to WindowView::processBackPressed (TODO).
    return false;
}

bool StageViewController::SupportWindowPrivacyMode()
{
    return false;
}

void StageViewController::SaveDumpFile(const std::vector<std::string>& dumpParams)
{
    LOGI("saveDumpFile enter");
    // Stage C: Wayland surface. The macOS port forwarded to
    // OHOS::Ace::Platform::DumpHelper::Dump(instanceId, params); wired with the
    // surface layer (TODO).
    LOGI("saveDumpFile pending Wayland surface layer, param count: %{public}zu", dumpParams.size());
}

void* StageViewController::GetWindowView()
{
    // Stage C: Wayland surface.
    return windowView_;
}
