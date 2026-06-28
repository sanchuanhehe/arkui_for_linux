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

// Linux/Wayland native port of the macOS Objective-C StageViewController
// (NSViewController). This is the per-ability window/view glue: on Linux the
// actual surface is a Wayland surface, which is not implemented in this layer.
// The class/method signatures the framework calls are preserved so this compiles;
// the window/view body is a TODO ("Stage C: Wayland surface"). The opaque view
// handles are kept as void* (the Wayland surface objects).

#ifndef FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_STAGEVIEWCONTROLLER_H
#define FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_STAGEVIEWCONTROLLER_H

#include <cstdint>
#include <string>
#include <vector>

class StageViewController {
public:
    /**
     * Initializes this StageViewController with the specified instance name.
     * instanceName(bundleName:moduleName:abilityName).
     */
    explicit StageViewController(const std::string& instanceName);
    ~StageViewController();

    StageViewController(const StageViewController&) = delete;
    StageViewController& operator=(const StageViewController&) = delete;

    const std::string& GetInstanceName() const;

    /**
     * Stage D: bring up the Wayland WindowView + RS surface node and drive the ability
     * lifecycle (AppMain DispatchOnCreate/Foreground) so the module .abc loads and its
     * page tree renders. Mirrors the macOS loadView/viewDidLoad.
     */
    void LoadView();

    /** Get the Id of StageViewController. */
    int32_t GetInstanceId() const;

    /** processBackPress: true if uicontent handled the back press. */
    bool ProcessBackPress();

    /** config privacy mode support; default false. */
    bool SupportWindowPrivacyMode();

    /** save dump file. */
    void SaveDumpFile(const std::vector<std::string>& dumpParams);

    /** The Wayland surface handle (opaque); nullptr until the surface is attached. */
    void* GetWindowView();

    // Kept for source parity with the macOS @property surface (mostly no-ops on
    // desktop: status bar / home indicator have no Wayland equivalent).
    bool statusBarHidden = false;
    bool homeIndicatorHidden = false;
    std::string params;
    bool privacyMode = false;

private:
    int32_t instanceId_ = 0;
    std::string instanceName_;
    std::string cInstanceName_;
    void* windowView_ = nullptr;          // WindowView (Wayland surface) - TODO Stage C
    void* stageContainerView_ = nullptr;  // StageContainerView - TODO Stage C
    bool needOnForeground_ = false;
};

#endif // FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_STAGEVIEWCONTROLLER_H
