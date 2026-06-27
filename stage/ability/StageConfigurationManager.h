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

// Linux/Wayland native port of the macOS Objective-C StageConfigurationManager.
// The original was an NSObject building an NSMutableDictionary configuration; the
// portable C++ form keeps a string->string map serialized to JSON. Orientation
// and device-idiom remain no-ops (no desktop equivalent), mirroring macOS M1.

#ifndef FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_CONFIGURATION_MANAGER_H
#define FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_CONFIGURATION_MANAGER_H

#include <cstdint>
#include <map>
#include <mutex>
#include <string>

// Linux has no UIUserInterfaceStyle enum; mirror its ordering so call sites that
// pass a "color mode" integer keep working: 0 = Unspecified, 1 = Light, 2 = Dark.
enum LinuxUserInterfaceStyle : int32_t {
    LinuxUserInterfaceStyleUnspecified = 0,
    LinuxUserInterfaceStyleLight = 1,
    LinuxUserInterfaceStyleDark = 2,
};

class StageConfigurationManager {
public:
    static StageConfigurationManager& GetInstance();

    // Linux: no-op. Orientation has no desktop equivalent; kept for source parity.
    void SetDirection(int32_t direction);

    // Linux: no-op. Orientation has no desktop equivalent; kept for source parity.
    void DirectionUpdate(int32_t direction);

    void SetColorMode(LinuxUserInterfaceStyle colorMode);

    void ColorModeUpdate(LinuxUserInterfaceStyle colorMode);

    // Linux: device-idiom (phone/tablet) has no desktop equivalent; kept for parity.
    void SetDeviceType(int32_t deviceType);

    void RegistConfiguration();

private:
    StageConfigurationManager() = default;
    ~StageConfigurationManager() = default;

    void SetLanguage(const std::string& language);
    void SetFontSizeScale(double fontScale);
    std::string GetCurrentLanguage();
    LinuxUserInterfaceStyle CurrentColorMode();
    std::string GetJsonString();

    std::mutex mutex_;
    std::map<std::string, std::string> configuration_;
};

#endif // FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_CONFIGURATION_MANAGER_H
