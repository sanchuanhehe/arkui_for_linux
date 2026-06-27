/*
 * Copyright (c) 2025-2025 Huawei Device Co., Ltd.
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

#include "AbilityLoader.h"

#include "app_main.h"

using AppMain = OHOS::AbilityRuntime::Platform::AppMain;
static const std::string ABILITY_LOADER_INSTANCE_ID = "100000";

void AbilityLoader::LoadAbility(const std::string& bundleName, const std::string& moduleName,
    const std::string& abilityName, const std::string& params)
{
    if (bundleName.empty()) {
        LOGE("load ability error: bundleName is invalid.");
        return;
    }
    if (moduleName.empty()) {
        LOGE("load ability error: moduleName is invalid.");
        return;
    }
    if (abilityName.empty()) {
        LOGE("load ability error: abilityName is invalid.");
        return;
    }
    std::string instanceName =
        bundleName + ":" + moduleName + ":" + abilityName + ":" + ABILITY_LOADER_INSTANCE_ID;
    NativeDispatchOnCreate(instanceName, params);
}

void AbilityLoader::UnloadAbility(
    const std::string& bundleName, const std::string& moduleName, const std::string& abilityName)
{
    if (bundleName.empty()) {
        LOGE("unload ability error: bundleName is invalid.");
        return;
    }
    if (moduleName.empty()) {
        LOGE("unload ability error: moduleName is invalid.");
        return;
    }
    if (abilityName.empty()) {
        LOGE("unload ability error: abilityName is invalid.");
        return;
    }
    std::string instanceName =
        bundleName + ":" + moduleName + ":" + abilityName + ":" + ABILITY_LOADER_INSTANCE_ID;
    NativeDispatchOnDestroy(instanceName);
}

void AbilityLoader::NativeDispatchOnCreate(const std::string& instanceName, const std::string& params)
{
    AppMain::GetInstance()->DispatchOnCreate(instanceName, params);
}

void AbilityLoader::NativeDispatchOnDestroy(const std::string& instanceName)
{
    AppMain::GetInstance()->DispatchOnDestroy(instanceName);
}
