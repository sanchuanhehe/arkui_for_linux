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

// Linux/Wayland native port of the macOS Objective-C AbilityLoader. The original
// was an NSObject exposing +loadAbility / +unloadAbility taking NSString*; the
// portable C++ form keeps the same static entry points taking std::string.

#ifndef FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_ABILITY_LOADER_H
#define FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_ABILITY_LOADER_H

#include <string>

class AbilityLoader {
public:
    static void LoadAbility(const std::string& bundleName, const std::string& moduleName,
        const std::string& abilityName, const std::string& params);

    static void UnloadAbility(
        const std::string& bundleName, const std::string& moduleName, const std::string& abilityName);

private:
    static void NativeDispatchOnCreate(const std::string& instanceName, const std::string& params);
    static void NativeDispatchOnDestroy(const std::string& instanceName);
};

#endif // FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_ABILITY_LOADER_H
