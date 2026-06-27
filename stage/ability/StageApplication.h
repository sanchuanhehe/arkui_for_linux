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

// Linux/Wayland native port of the macOS Objective-C StageApplication. The
// original was an NSObject mixing portable launch/locale/delegator logic with
// AppKit view-controller traversal. The portable launch/asset/locale/module paths
// are ported here; the methods that walk the NSWindow / NSViewController tree are
// kept as signatures but stubbed pending the Wayland surface layer.

#ifndef FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_APPLICATION_H
#define FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_APPLICATION_H

#include <string>

#include "StageViewController.h"

class StageApplication {
public:
    static void ConfigModuleWithBundleDirectory(const std::string& bundleDirectory);

    static void LaunchApplication();

    static void LaunchApplicationWithoutUI();

    static void CallCurrentAbilityOnForeground();

    static void CallCurrentAbilityOnBackground();

    static bool HandleSingleton(
        const std::string& bundleName, const std::string& moduleName, const std::string& abilityName);

    static void ReleaseViewControllers();

    static StageViewController* GetApplicationTopViewController();

    static void SetLocaleWithLanguage(
        const std::string& language, const std::string& country, const std::string& script);

    std::string GetTopAbility();

    void DoAbilityForeground(const std::string& fullname);

    void DoAbilityBackground(const std::string& fullname);

    void Print(const std::string& msg);

    void PrintSync(const std::string& msg);

    int FinishTest();

    static void PreloadEtsModule(const std::string& moduleName, const std::string& abilityName);

    static void LoadModule(const std::string& moduleName, const std::string& entryFile);

private:
    static void InitApplication(bool isLoadArkUI);
    static void StartAbilityDelegator();
    static void SetPidAndUid();
    static void SetLocale();
};

#endif // FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_APPLICATION_H
