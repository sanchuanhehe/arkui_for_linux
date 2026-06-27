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

// Linux/Wayland native port of the macOS ability_context_adapter.mm. The macOS
// version launched abilities through NSURLComponents + [NSWorkspace openURL:] and
// pushed work to the main GCD queue. On Linux there is no NSWorkspace/GCD: the
// portable string/dispatch logic is kept, while the actual cross-process URL
// launch is deferred to the Wayland surface/launcher layer.

#include "ability_context_adapter.h"

#include <cstdio>
#include <string>
#include <vector>

#include "StageApplication.h"
#include "ability_manager_errors.h"
#include "app_main.h"
#include "base/utils/string_utils.h"

namespace OHOS::AbilityRuntime::Platform {
namespace {
const std::string URL_QUERY_ABILITY_KEY = "abilityName";
const std::string URL_QUERY_PARAMS_KEY = "params";
const std::string BUNDLENAME_FILEPICKER = "com.ohos.filepicker";
const std::string BUNDLENAME_PHOTOPICKER = "com.ohos.photos";
const std::string BUNDLENAME_HYPERLINK = "com.ohos.hyperlink";
} // namespace

std::shared_ptr<AbilityContextAdapter> AbilityContextAdapter::instance_ = nullptr;
std::mutex AbilityContextAdapter::mutex_;

std::shared_ptr<AbilityContextAdapter> AbilityContextAdapter::GetInstance()
{
    if (instance_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instance_ == nullptr) {
            instance_ = std::make_shared<AbilityContextAdapter>();
        }
    }

    return instance_;
}

void AbilityContextAdapter::print(const std::string& message)
{
    LOGI("AbilityContextAdapter print, msg : %{public}s", message.c_str());
    StageApplication application;
    application.Print(message);
}

size_t AbilityContextAdapter::StringToken(std::string& str, const std::string& sep, std::string& token)
{
    token = "";
    if (str.empty()) {
        return str.npos;
    }
    size_t pos = str.npos;
    size_t tmp = 0;
    for (auto& item : sep) {
        tmp = str.find(item);
        if (str.npos != tmp) {
            pos = (std::min)(pos, tmp);
        }
    }
    if (str.npos != pos) {
        token = str.substr(0, pos);
        if (str.npos != pos + 1) {
            str = str.substr(pos + 1, str.npos);
        }
        if (pos == 0) {
            return StringToken(str, sep, token);
        }
    } else {
        token = str;
        str = "";
    }
    return token.size();
}

size_t AbilityContextAdapter::StringSplit(
    const std::string& str, const std::string& sep, std::vector<std::string>& vecList)
{
    size_t size;
    auto strs = str;
    std::string token;
    while (str.npos != (size = StringToken(strs, sep, token))) {
        vecList.push_back(token);
    }
    return vecList.size();
}

int32_t AbilityContextAdapter::StartAbility(const std::string& instanceName, const AAFwk::Want& want)
{
    std::string bundleName = want.GetBundleName();
    std::string moduleName = want.GetModuleName();
    std::string abilityName = want.GetAbilityName();
    std::string jsonString = want.ToJson();

    // Linux: file/photo picker is dropped (no desktop picker wired in M1); only
    // URL-based launch + hyperlink are kept.
    if (bundleName == BUNDLENAME_FILEPICKER || bundleName == BUNDLENAME_PHOTOPICKER) {
        LOGI("StartAbility: picker bundle dropped on Linux M1: %{public}s", bundleName.c_str());
        return ERR_OK;
    } else if (bundleName == BUNDLENAME_HYPERLINK) {
        StartAbilityForHyperlink(want);
    } else {
        if (bundleName.empty() || moduleName.empty() || abilityName.empty()) {
            LOGE("startAbility failed, bundleName : %{public}s, moduleName : %{public}s, abilityName : %{public}s",
                bundleName.c_str(), moduleName.c_str(), abilityName.c_str());
            return AAFwk::RESOLVE_ABILITY_ERR;
        }

        std::string appUrl = bundleName + "://" + moduleName + "?" + URL_QUERY_ABILITY_KEY + "=" + abilityName;
        if (!jsonString.empty()) {
            appUrl += "&" + URL_QUERY_PARAMS_KEY + "=" + jsonString;
        }
        // Stage C: Wayland surface. The macOS port handed this URL to
        // [NSWorkspace openURL:] on the main queue; the cross-process launch path
        // (e.g. xdg-open) belongs to the Wayland launcher layer (TODO).
        LOGI("startAbility, app url: %{public}s (launch pending Wayland layer)", appUrl.c_str());
    }
    return ERR_OK;
}

int32_t AbilityContextAdapter::StartAbilityForHyperlink(const AAFwk::Want& want)
{
    std::string uri = want.GetUri();
    if (uri.empty()) {
        LOGE("No available app found for implicit start");
        return AAFwk::RESOLVE_ABILITY_ERR;
    }
    // Stage C: Wayland surface. Opening an external URI belongs to the Wayland
    // launcher layer (TODO, e.g. xdg-open).
    LOGI("startAbilityForHyperlink, uri: %{public}s (launch pending Wayland layer)", uri.c_str());
    return ERR_OK;
}

std::string AbilityContextAdapter::GetTopAbility()
{
    StageApplication application;
    std::string resultString = application.GetTopAbility();
    if (resultString.empty()) {
        resultString = "GetTopAbility error";
    }
    return resultString;
}

int32_t AbilityContextAdapter::DoAbilityForeground(const std::string& fullname)
{
    StageApplication application;
    application.DoAbilityForeground(fullname);
    return ERR_OK;
}

int32_t AbilityContextAdapter::DoAbilityBackground(const std::string& fullname)
{
    std::string instanceName = GetTopAbility();
    auto pos = instanceName.find(fullname);
    if (pos == std::string::npos) {
        LOGI("Do ability background, already background %{public}s", fullname.c_str());
        return ERR_OK;
    }

    StageApplication application;
    application.DoAbilityBackground(fullname);
    return ERR_OK;
}

void AbilityContextAdapter::DoAbilityPrint(const std::string& msg)
{
    StageApplication application;
    application.Print(msg);
}

void AbilityContextAdapter::DoAbilityPrintSync(const std::string& msg)
{
    StageApplication application;
    application.PrintSync(msg);
}

int32_t AbilityContextAdapter::FinishUserTest()
{
    StageApplication application;
    int error = application.FinishTest();
    return error;
}

void AbilityContextAdapter::TerminateSelf(const std::string& instanceName)
{
    // Stage C: Wayland surface. The macOS port marshalled this onto the main GCD
    // queue; on Linux/Wayland thread affinity is owned by the surface layer.
    LOGI("%{public}s, terminate instance: %{public}s", __func__, instanceName.c_str());
    OHOS::AbilityRuntime::Platform::AppMain::GetInstance()->DispatchOnDestroy(instanceName);
}

int32_t AbilityContextAdapter::StartAbilityForResult(
    const std::string& instanceName, const AAFwk::Want& want, int32_t requestCode)
{
    // Linux: no-op (startAbilityForResult relied on the picker flow, dropped in M1)
    LOGI("%{public}s, dropped on Linux M1", __func__);
    return ERR_OK;
}

int32_t AbilityContextAdapter::TerminateAbilityWithResult(
    const std::string& instanceName, const AAFwk::Want& resultWant, int32_t resultCode)
{
    return ERR_OK;
}

int32_t AbilityContextAdapter::ReportDrawnCompleted(const std::string& instanceName)
{
    return ERR_OK;
}

std::string AbilityContextAdapter::GetPlatformBundleName()
{
    return "";
}
} // namespace OHOS::AbilityRuntime::Platform
