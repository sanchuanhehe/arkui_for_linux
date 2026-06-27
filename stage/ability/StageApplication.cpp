/*
 * Copyright (c) 2023-2026 Huawei Device Co., Ltd.
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

#include "StageApplication.h"

#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>
#include <vector>
#include <unistd.h>

#include "StageAssetManager.h"
#include "StageConfigurationManager.h"
#include "app_main.h"
#include "stage_application_info_adapter.h"

using AppMain = OHOS::AbilityRuntime::Platform::AppMain;
static const std::string kEtsPathRegexPattern = "^\\./ets/([^/]+/)*[^/]+$";
static bool g_isOnBackground = false;

namespace {
// Read the process command line (/proc/self/cmdline) into argv-like tokens. The
// macOS port used NSProcessInfo.arguments.
std::vector<std::string> GetProcessArguments()
{
    std::vector<std::string> args;
    std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
    if (!cmdline.is_open()) {
        return args;
    }
    std::string token;
    char ch = 0;
    while (cmdline.get(ch)) {
        if (ch == '\0') {
            args.emplace_back(token);
            token.clear();
        } else {
            token.push_back(ch);
        }
    }
    if (!token.empty()) {
        args.emplace_back(token);
    }
    return args;
}

// Read the desktop locale tag (e.g. "zh-CN", "en-US") from the POSIX environment.
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

bool StartsWith(const std::string& str, const std::string& prefix)
{
    return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}
} // namespace

void StageApplication::ConfigModuleWithBundleDirectory(const std::string& bundleDirectory)
{
    LOGI("%{public}s bundleDirectory : %{public}s", __func__, bundleDirectory.c_str());
    StageAssetManager::GetInstance().ModuleFilesWithBundleDirectory(bundleDirectory);
    OHOS::AbilityRuntime::Platform::AppMain::GetInstance()->SetResourceFilePrefixPath();
}

void StageApplication::LaunchApplication()
{
    LOGI("%{public}s", __FUNCTION__);
    InitApplication(true);
}

void StageApplication::LaunchApplicationWithoutUI()
{
    LOGI("%{public}s", __FUNCTION__);
    InitApplication(false);
}

void StageApplication::InitApplication(bool isLoadArkUI)
{
    SetPidAndUid();
    SetLocale();
    // Stage C: Wayland surface. The macOS port wired NSApplication hide/unhide
    // notifications here to drive app foreground/background; on Linux/Wayland those
    // transitions come from the surface/compositor layer (TODO).
    StageAssetManager::GetInstance().LaunchAbility(isLoadArkUI);
    StageConfigurationManager::GetInstance().RegistConfiguration();
    StartAbilityDelegator();
}

void StageApplication::StartAbilityDelegator()
{
    std::vector<std::string> arguments = GetProcessArguments();
    bool hasTest = false;
    for (const auto& arg : arguments) {
        if (arg == "test") {
            hasTest = true;
            break;
        }
    }
    if (!hasTest) {
        LOGI("%{public}s, No need to start creating abilityDelegate", __FUNCTION__);
        return;
    }
    std::string bundleName;
    std::string moduleName;
    std::string unittest;
    std::string timeout;
    std::string socket;
    for (size_t i = 1; i < arguments.size(); i++) {
        if (arguments[i] == "bundleName" && i + 1 < arguments.size()) {
            bundleName = arguments[i + 1];
        } else if (arguments[i] == "moduleName" && i + 1 < arguments.size()) {
            moduleName = arguments[i + 1];
        } else if (arguments[i] == "unittest" && i + 1 < arguments.size()) {
            unittest = arguments[i + 1];
        } else if (arguments[i] == "timeout" && i + 1 < arguments.size()) {
            timeout = arguments[i + 1];
        } else if (arguments[i] == "socket" && i + 1 < arguments.size()) {
            socket = arguments[i + 1];
        }
    }
    AppMain::GetInstance()->PrepareAbilityDelegator(bundleName, moduleName, unittest, timeout, socket);
}

void StageApplication::SetPidAndUid()
{
    int pid = getpid();
    int32_t uid = 0;
    LOGI("%{public}s pid : %{public}d", __func__, pid);
    OHOS::AbilityRuntime::Platform::AppMain::GetInstance()->SetPidAndUid(pid, uid);
}

void StageApplication::SetLocale()
{
    std::string currentLanguage = ReadEnvLanguage();
    std::vector<std::string> array = SplitString(currentLanguage, '-');
    std::string language;
    std::string country;
    std::string script;

    if (StartsWith(currentLanguage, "zh-Hans")) {
        language = "zh";
        country = "CN";
        script = "Hans";
    } else if (StartsWith(currentLanguage, "zh-HK") || StartsWith(currentLanguage, "zh-Hant-HK")) {
        language = "zh";
        country = "HK";
        script = "Hant";
    } else if (StartsWith(currentLanguage, "zh-TW") || StartsWith(currentLanguage, "zh-Hant")) {
        language = "zh";
        country = "TW";
        script = "Hant";
    } else if (array.size() == 1) {
        language = array[0];
    } else if (array.size() == 2) {
        language = array[0];
        country = array[1];
    } else if (array.size() == 3) {
        language = array[0];
        country = array[2];
        script = array[1];
    }
    // Linux gives "zh-CN" (no script subtag); fill the script for Chinese so the
    // resource manager can resolve Hans/Hant.
    if (language == "zh" && script.empty()) {
        if (country == "TW" || country == "HK" || country == "MO") {
            script = "Hant";
        } else {
            script = "Hans";
        }
    }
    LOGI("%{public}s, language : %{public}s, country : %{public}s script : %{public}s", __FUNCTION__, language.c_str(),
        country.c_str(), script.c_str());
    OHOS::AbilityRuntime::Platform::StageApplicationInfoAdapter::GetInstance()->SetLocale(language, country, script);
}

void StageApplication::SetLocaleWithLanguage(
    const std::string& language, const std::string& country, const std::string& script)
{
    OHOS::AbilityRuntime::Platform::StageApplicationInfoAdapter::GetInstance()->SetLocale(language, country, script);
}

void StageApplication::CallCurrentAbilityOnForeground()
{
    if (!g_isOnBackground) {
        return;
    }
    g_isOnBackground = false;
    // Stage C: Wayland surface. The macOS port queried the top StageViewController
    // and dispatched OnForeground for its instance; the top-window lookup belongs
    // to the Wayland surface layer (TODO).
    LOGI("%{public}s, top window lookup pending Wayland surface layer", __FUNCTION__);
}

void StageApplication::CallCurrentAbilityOnBackground()
{
    if (g_isOnBackground) {
        return;
    }
    g_isOnBackground = true;
    // Stage C: Wayland surface. See CallCurrentAbilityOnForeground (TODO).
    LOGI("%{public}s, top window lookup pending Wayland surface layer", __FUNCTION__);
}

bool StageApplication::HandleSingleton(
    const std::string& bundleName, const std::string& moduleName, const std::string& abilityName)
{
    bool isSingle = AppMain::GetInstance()->IsSingleton(moduleName, abilityName);
    if (!isSingle) {
        return false;
    }
    std::string singleName = bundleName + ":" + moduleName + ":" + abilityName;
    LOGI("%{public}s, singleName is %{public}s", __func__, singleName.c_str());
    // Stage C: Wayland surface. Matching the singleton against the live top window
    // and dispatching OnNewWant needs the Wayland surface layer (TODO).
    return false;
}

void StageApplication::ReleaseViewControllers()
{
    // Stage C: Wayland surface. The macOS port destroyed the top
    // StageViewController instance; the live window stack belongs to the Wayland
    // surface layer (TODO).
    LOGI("%{public}s, pending Wayland surface layer", __FUNCTION__);
}

StageViewController* StageApplication::GetApplicationTopViewController()
{
    // Stage C: Wayland surface. The macOS port returned the key/main NSWindow's
    // content view controller; there is no window stack yet on Linux/Wayland.
    return nullptr;
}

std::string StageApplication::GetTopAbility()
{
    // Stage C: Wayland surface. Depends on GetApplicationTopViewController (TODO).
    return "current views is null";
}

void StageApplication::DoAbilityForeground(const std::string& fullname)
{
    // Stage C: Wayland surface. Window reordering belongs to the surface layer.
}

void StageApplication::DoAbilityBackground(const std::string& fullname)
{
    // Stage C: Wayland surface. Window reordering belongs to the surface layer.
}

void StageApplication::Print(const std::string& msg)
{
    if (msg.length() >= 1000) {
        LOGI("print: The total length of the message exceed 1000 characters.");
    } else {
        LOGI("print: %{public}s", msg.c_str());
    }
}

void StageApplication::PrintSync(const std::string& msg)
{
    if (msg.length() >= 1000) {
        LOGI("printSync: The total length of the message exceed 1000 characters.");
    } else {
        LOGI("printSync: %{public}s", msg.c_str());
    }
}

int StageApplication::FinishTest()
{
    LOGI("TestFinished-ResultMsg: your test finished!!!");
    exit(0);
    return 0;
}

void StageApplication::PreloadEtsModule(const std::string& moduleName, const std::string& abilityName)
{
    if (moduleName.empty()) {
        LOGE("moduleName is null");
        return;
    }
    if (abilityName.empty()) {
        LOGE("abilityName is null");
        return;
    }
    AppMain::GetInstance()->PreloadModule(moduleName, abilityName);
}

void StageApplication::LoadModule(const std::string& moduleName, const std::string& entryFile)
{
    if (moduleName.empty()) {
        LOGE("load module error: moduleName is null.");
        return;
    }
    if (entryFile.empty()) {
        LOGE("load module error: path is null.");
        return;
    }
    try {
        std::regex regex(kEtsPathRegexPattern);
        if (!std::regex_match(entryFile, regex)) {
            LOGE("load module error: path is invalid.");
            return;
        }
    } catch (const std::regex_error& e) {
        LOGE("load module error: %{public}s", e.what());
        return;
    }
    AppMain::GetInstance()->LoadModule(moduleName, entryFile);
}
