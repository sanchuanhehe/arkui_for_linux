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

#include "StageAssetManager.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <system_error>
#include <unistd.h>

#include "app_main.h"
#include "nlohmann/json.hpp"

using AppMain = OHOS::AbilityRuntime::Platform::AppMain;

namespace {
const std::string PKG_CONTEXT_INFO_JSON = "pkgContextInfo.json";
const std::string FILTER_FILE_MODULE_JSON = "module.json";
const std::string FILTER_FILE_ABILITYSTAGE_ABC = "AbilityStage.abc";
const std::string MODULE_STAGE_ABC_NAME = "modules.abc";
const std::string FILTER_FILE_MODULE_ABC = ".abc";
const std::string FILTER_FILE_RESOURCES_INDEX = "resources.index";
const std::string FILTER_FILE_SYSTEM_RESOURCES_INDEX = "systemres";
const std::string DOCUMENTS_FONTS_FILES = "fonts";
const std::string DOCUMENTS_SUBDIR_FILES = "files";
const std::string DOCUMENTS_SUBDIR_DATABASE = "database";

// The macOS port read [NSBundle mainBundle].bundlePath; on Linux the resources sit
// next to the executable (mirrors resource_path_util.cpp).
std::string GetExecutableDir()
{
    char buf[PATH_MAX] = { 0 };
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) {
        return ".";
    }
    buf[len] = '\0';
    std::string path(buf);
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? std::string(".") : path.substr(0, pos);
}

// The macOS port used NSDocumentDirectory; on Linux the writable sandbox follows
// the XDG base dir spec (mirrors resource_path_util.cpp), rooted at an "arkui-x"
// subdir which then holds files/ and database/.
std::string GetDocumentsDirectory()
{
    const char* xdgData = getenv("XDG_DATA_HOME");
    std::string base;
    if (xdgData != nullptr && xdgData[0] != '\0') {
        base = xdgData;
    } else {
        const char* home = getenv("HOME");
        base = std::string(home != nullptr ? home : ".") + "/.local/share";
    }
    return base + "/arkui-x";
}

std::string LastPathComponent(const std::string& path)
{
    auto pos = path.find_last_of('/');
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

bool Contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

template<class T>
void AppendUnique(std::vector<T>& target, const T& value)
{
    if (std::find(target.begin(), target.end(), value) == target.end()) {
        target.emplace_back(value);
    }
}
} // namespace

StageAssetManager& StageAssetManager::GetInstance()
{
    static StageAssetManager instance;
    return instance;
}

void StageAssetManager::ModuleFilesWithBundleDirectory(const std::string& bundleDirectory)
{
    std::vector<std::string> files;
    std::string bundlePath = GetExecutableDir() + "/" + bundleDirectory;
    LOGI("%{public}s, \n bundlePath is : %{public}s", __func__, bundlePath.c_str());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bundlePath_ = bundlePath;
    }

    std::error_code ec;
    if (std::filesystem::exists(bundlePath, ec) && std::filesystem::is_directory(bundlePath, ec)) {
        for (auto it = std::filesystem::recursive_directory_iterator(
                 bundlePath, std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) {
                break;
            }
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc) || fileEc) {
                continue;
            }
            std::string filePath = it->path().string();
            std::string fileName = LastPathComponent(filePath);
            if (fileName == FILTER_FILE_MODULE_JSON) {
                std::lock_guard<std::mutex> lock(mutex_);
                AppendUnique(moduleJsonFileArray_, filePath);
            } else if (fileName == PKG_CONTEXT_INFO_JSON) {
                std::lock_guard<std::mutex> lock(mutex_);
                AppendUnique(pkgJsonFileArray_, filePath);
            }
            files.emplace_back(filePath);
        }
    }
    LOGI("%{public}s, all files count : %{public}zu", __func__, files.size());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& file : files) {
            AppendUnique(allModuleFilePathArray_, file);
        }
    }

    bool isCreatFiles = CreateDocumentSubDirectory(DOCUMENTS_SUBDIR_FILES);
    bool isCreatDatabase = CreateDocumentSubDirectory(DOCUMENTS_SUBDIR_DATABASE);
    LOGI("isCreatFiles : %{public}d, isCreatDatabase : %{public}d", isCreatFiles, isCreatDatabase);
}

std::vector<uint8_t> StageAssetManager::UpdateModuleNameWithJsonData(
    const std::vector<uint8_t>& data, const std::string& moduleJsonPath)
{
    if (data.empty()) {
        return data;
    }
    nlohmann::json jsonDict = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
    if (jsonDict.is_discarded() || !jsonDict.is_object()) {
        return data;
    }
    if (!jsonDict.contains("module") || !jsonDict["module"].is_object() || !jsonDict.contains("app") ||
        !jsonDict["app"].is_object()) {
        return data;
    }
    auto& moduleObj = jsonDict["module"];
    auto& appObj = jsonDict["app"];
    if (!moduleObj.contains("name") || !appObj.contains("bundleName")) {
        return data;
    }
    std::string moduleName = moduleObj["name"].get<std::string>();
    std::string bundleName = appObj["bundleName"].get<std::string>();
    std::string fullModuleName = bundleName + "." + moduleName;
    if (moduleJsonPath.empty() || !Contains(moduleJsonPath, fullModuleName)) {
        return data;
    }
    std::string packageName = moduleObj.contains("packageName") ? moduleObj["packageName"].get<std::string>() : "";
    moduleObj["name"] = fullModuleName;
    moduleObj["packageName"] = bundleName + "." + packageName;

    std::string updated = jsonDict.dump();
    return std::vector<uint8_t>(updated.begin(), updated.end());
}

void StageAssetManager::LaunchAbility(bool isLoadArkUI)
{
    LOGI("%{public}s", __func__);
    AppMain::GetInstance()->LaunchApplication(true, isLoadArkUI);
}

std::string StageAssetManager::GetResourceFilePrefixPath()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return bundlePath_ + "/" + FILTER_FILE_SYSTEM_RESOURCES_INDEX + "/" + DOCUMENTS_FONTS_FILES;
}

std::string StageAssetManager::GetBundlePath()
{
    LOGI("%{public}s", __func__);
    std::lock_guard<std::mutex> lock(mutex_);
    return bundlePath_;
}

std::vector<std::string> StageAssetManager::GetAssetAllFilePathList()
{
    std::lock_guard<std::mutex> lock(mutex_);
    LOGI("%{public}s, \n all asset file list size: %{public}zu", __func__, allModuleFilePathArray_.size());
    return allModuleFilePathArray_;
}

std::vector<std::string> StageAssetManager::GetPkgJsonFileList()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pkgJsonFileArray_;
}

std::vector<std::string> StageAssetManager::GetModuleJsonFileList()
{
    std::lock_guard<std::mutex> lock(mutex_);
    LOGI("%{public}s, \n modulejson file list", __func__);
    return moduleJsonFileArray_;
}

std::string StageAssetManager::GetAbilityStageABC(
    const std::string& moduleName, std::string& modulePath, bool esmodule)
{
    LOGI("%{public}s, moduleName : %{public}s", __func__, moduleName.c_str());
    if (moduleName.empty()) {
        return "";
    }
    std::vector<std::string> array = GetAssetAllFilePathList();
    if (array.empty()) {
        LOGI("%{public}s, allModuleFilePathArray null", __func__);
        return "";
    }
    std::string moduleString = esmodule ? MODULE_STAGE_ABC_NAME : FILTER_FILE_ABILITYSTAGE_ABC;
    std::string moduleSegment = "/" + moduleName + "/";
    for (const auto& path : array) {
        if (Contains(path, moduleSegment) && Contains(path, moduleString)) {
            LOGI("%{public}s, moduleName : %{public}s, \n AbilityStage.abc  : %{public}s", __func__,
                moduleName.c_str(), path.c_str());
            modulePath = path;
            return path;
        }
    }
    return "";
}

std::string StageAssetManager::GetModuleAbilityABC(
    const std::string& moduleName, const std::string& abilityName, std::string& modulePath, bool esmodule)
{
    LOGI("%{public}s, moduleName : %{public}s, abilityName : %{public}s", __func__, moduleName.c_str(),
        abilityName.c_str());
    if (moduleName.empty() || abilityName.empty()) {
        return "";
    }
    std::vector<std::string> array = GetAssetAllFilePathList();
    if (array.empty()) {
        LOGI("%{public}s, allModuleFilePathArray null", __func__);
        return "";
    }
    std::string moduleSegment = "/" + moduleName + "/";
    if (!esmodule) {
        for (const auto& path : array) {
            if (Contains(path, moduleSegment) && Contains(path, abilityName) &&
                Contains(path, FILTER_FILE_MODULE_ABC)) {
                LOGI("%{public}s, moduleName : %{public}s, abilityName : %{public}s, \n path : %{public}s", __func__,
                    moduleName.c_str(), abilityName.c_str(), path.c_str());
                modulePath = path;
                return path;
            }
        }
    } else {
        for (const auto& path : array) {
            if (Contains(path, moduleSegment) && Contains(path, MODULE_STAGE_ABC_NAME)) {
                LOGI("%{public}s, moduleName : %{public}s, abilityName : %{public}s, \n path : %{public}s", __func__,
                    moduleName.c_str(), abilityName.c_str(), path.c_str());
                modulePath = path;
                return path;
            }
        }
    }
    return "";
}

void StageAssetManager::GetModuleResources(
    const std::string& moduleName, std::string& appResIndexPath, std::string& sysResIndexPath)
{
    LOGI("%{public}s, moduleName : %{public}s", __func__, moduleName.c_str());
    if (moduleName.empty()) {
        return;
    }
    std::vector<std::string> array = GetAssetAllFilePathList();
    if (array.empty()) {
        LOGI("%{public}s, allModuleFilePathArray null", __func__);
        return;
    }
    std::string moduleSegment = "/" + moduleName + "/";
    for (const auto& path : array) {
        if (Contains(path, moduleSegment) && Contains(path, FILTER_FILE_RESOURCES_INDEX)) {
            appResIndexPath = path;
            LOGI("%{public}s, moduleName : %{public}s, \n appResIndexPath : %{public}s", __func__, moduleName.c_str(),
                path.c_str());
            continue;
        }
        if (Contains(path, FILTER_FILE_RESOURCES_INDEX) && Contains(path, FILTER_FILE_SYSTEM_RESOURCES_INDEX)) {
            LOGI("%{public}s, moduleName : %{public}s, \n sysResIndexPath : %{public}s", __func__, moduleName.c_str(),
                path.c_str());
            sysResIndexPath = path;
            continue;
        }
    }
}

bool StageAssetManager::CreateDocumentSubDirectory(const std::string& path)
{
    std::string targetDirectory = GetDocumentsDirectory() + "/" + path;
    std::error_code ec;
    std::filesystem::create_directories(targetDirectory, ec);
    if (ec) {
        return false;
    }
    return std::filesystem::is_directory(targetDirectory, ec) && !ec;
}

bool StageAssetManager::IsExistFileForPath(const std::string& filePath)
{
    if (filePath.empty()) {
        return false;
    }
    std::error_code ec;
    return std::filesystem::is_regular_file(filePath, ec) && !ec;
}
