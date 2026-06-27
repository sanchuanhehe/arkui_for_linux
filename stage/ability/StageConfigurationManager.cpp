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

#include "StageConfigurationManager.h"

#include <cstdio>
#include <cstdlib>

#include "app_main.h"
#include "nlohmann/json.hpp"

using AppMain = OHOS::AbilityRuntime::Platform::AppMain;

namespace {
const std::string COLOR_MODE_LIGHT = "light";
const std::string COLOR_MODE_DARK = "dark";
const std::string EMPTY_JSON = "";
const std::string UNKNOWN = "";
const std::string APPLICATION_DIRECTION = "ohos.application.direction";
const std::string SYSTEM_COLORMODE = "ohos.system.colorMode";
const std::string SYSTEM_LANGUAGE = "ohos.system.language";
const std::string SYSTEM_FONT_SIZE_SCALE = "system.font.size.scale";

// Read the desktop locale from the POSIX environment (LC_ALL > LANG), returning a
// BCP-47-ish tag such as "zh-CN" or "en-US". The macOS original used
// [NSLocale preferredLanguages].
std::string ReadEnvLanguage()
{
    const char* env = getenv("LC_ALL");
    if (env == nullptr || env[0] == '\0') {
        env = getenv("LANG");
    }
    if (env == nullptr || env[0] == '\0') {
        return "en-US";
    }
    std::string value(env);
    // Strip charset suffix: "zh_CN.UTF-8" -> "zh_CN"; modifier: "ca_ES@valencia".
    auto dot = value.find('.');
    if (dot != std::string::npos) {
        value = value.substr(0, dot);
    }
    auto at = value.find('@');
    if (at != std::string::npos) {
        value = value.substr(0, at);
    }
    for (auto& ch : value) {
        if (ch == '_') {
            ch = '-';
        }
    }
    if (value.empty() || value == "C" || value == "POSIX") {
        return "en-US";
    }
    return value;
}
} // namespace

StageConfigurationManager& StageConfigurationManager::GetInstance()
{
    static StageConfigurationManager instance;
    return instance;
}

void StageConfigurationManager::RegistConfiguration()
{
    LOGI("initConfiguration called");
    // Linux: orientation/device-idiom dropped (no desktop equivalent); report a
    // desktop color mode + language.
    SetColorMode(CurrentColorMode());
    SetLanguage(GetCurrentLanguage());
    SetFontSizeScale(1.0);
    std::string json = GetJsonString();

    if (json.empty()) {
        AppMain::GetInstance()->InitConfiguration(EMPTY_JSON);
    }
    AppMain::GetInstance()->InitConfiguration(json);
}

void StageConfigurationManager::DirectionUpdate(int32_t direction)
{
    // Linux: no-op. Orientation has no desktop equivalent.
}

void StageConfigurationManager::ColorModeUpdate(LinuxUserInterfaceStyle colorMode)
{
    LOGI("colorModeUpdate called");
    SetColorMode(colorMode);
    std::string json = GetJsonString();
    if (json.empty()) {
        AppMain::GetInstance()->OnConfigurationUpdate(EMPTY_JSON);
    }
    AppMain::GetInstance()->OnConfigurationUpdate(json);
}

void StageConfigurationManager::SetDirection(int32_t direction)
{
    // Linux: no-op. Orientation has no desktop equivalent.
}

void StageConfigurationManager::SetLanguage(const std::string& language)
{
    std::lock_guard<std::mutex> lock(mutex_);
    configuration_[SYSTEM_LANGUAGE] = language;
}

void StageConfigurationManager::SetFontSizeScale(double fontScale)
{
    std::lock_guard<std::mutex> lock(mutex_);
    char buffer[16] = { 0 };
    snprintf(buffer, sizeof(buffer), "%.2f", fontScale);
    configuration_[SYSTEM_FONT_SIZE_SCALE] = std::string(buffer);
}

std::string StageConfigurationManager::GetCurrentLanguage()
{
    return ReadEnvLanguage();
}

// Resolve the effective desktop appearance into the mirrored enum. There is no
// portable system-wide dark-mode signal on Linux/Wayland; honor an explicit
// override env var, otherwise default to light (matching macOS M1 desktop).
LinuxUserInterfaceStyle StageConfigurationManager::CurrentColorMode()
{
    const char* mode = getenv("ARKUIX_COLOR_MODE");
    if (mode != nullptr) {
        std::string value(mode);
        if (value == "dark") {
            return LinuxUserInterfaceStyleDark;
        }
    }
    return LinuxUserInterfaceStyleLight;
}

void StageConfigurationManager::SetColorMode(LinuxUserInterfaceStyle colorMode)
{
    std::lock_guard<std::mutex> lock(mutex_);
    switch (colorMode) {
        case LinuxUserInterfaceStyleLight:
            configuration_[SYSTEM_COLORMODE] = COLOR_MODE_LIGHT;
            break;
        case LinuxUserInterfaceStyleDark:
            configuration_[SYSTEM_COLORMODE] = COLOR_MODE_DARK;
            break;
        default:
            configuration_[APPLICATION_DIRECTION] = UNKNOWN;
            break;
    }
}

void StageConfigurationManager::SetDeviceType(int32_t deviceType)
{
    // Linux: no-op. Device idiom (phone/tablet) has no desktop equivalent.
}

std::string StageConfigurationManager::GetJsonString()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (configuration_.empty()) {
        return EMPTY_JSON;
    }
    nlohmann::json json;
    for (const auto& kv : configuration_) {
        json[kv.first] = kv.second;
    }
    return json.dump();
}
