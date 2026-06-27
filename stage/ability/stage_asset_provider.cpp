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

// Linux/Wayland native port of the macOS stage_asset_provider.mm. NSData file IO
// becomes std::ifstream, NSFileManager directory walks become std::filesystem, and
// the NSSearchPath/NSBundle directories become executable-relative + XDG base
// paths (mirroring resource_path_util.cpp). All JSON / source-map merge logic was
// already portable and is unchanged.

#include "stage_asset_provider.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unistd.h>

#include "StageAssetManager.h"
#include "base/log/log_wrapper.h"
#include "base/utils/string_utils.h"
#include "nlohmann/json.hpp"

namespace OHOS {
namespace AbilityRuntime {
namespace Platform {
static const std::string SOURCE_MAP_RELATIVE_PATH = "ets/sourceMaps.map";
static const std::string PATH_SEPARATOR = "/";
static const std::string BACKSLASH_PATH_SEPARATOR = "\\";
static const std::string PARENT_PATH_SEGMENT = "..";
static const std::string JSON_WHITESPACE_CHARS = " \t\r\n";
static constexpr char JSON_OBJECT_BEGIN = '{';
static constexpr char JSON_OBJECT_END = '}';
static constexpr char JSON_MEMBER_SEPARATOR = ',';
static constexpr size_t JSON_OBJECT_BOUNDARY_LENGTH = 2;

std::shared_ptr<StageAssetProvider> StageAssetProvider::instance_ = nullptr;
std::mutex StageAssetProvider::mutex_;
std::map<std::string, std::pair<std::string, std::string>> StageAssetProvider::staticResCache = {};

namespace {
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

// XDG-based writable root, rooted at an "arkui-x" subdir (mirrors
// resource_path_util.cpp and StageAssetManager.cpp). Replaces NSDocumentDirectory.
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

std::vector<uint8_t> ReadFileToVector(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}
} // namespace

static bool IsValidSourceMapModuleName(const std::string& moduleName)
{
    return moduleName.find(PATH_SEPARATOR) == std::string::npos &&
        moduleName.find(BACKSLASH_PATH_SEPARATOR) == std::string::npos &&
        moduleName.find(PARENT_PATH_SEGMENT) == std::string::npos;
}

static bool GetJsonObjectBodyRange(const std::string& jsonContent, size_t& bodyBegin, size_t& bodyEnd)
{
    auto objectBegin = jsonContent.find_first_not_of(JSON_WHITESPACE_CHARS);
    auto objectEnd = jsonContent.find_last_not_of(JSON_WHITESPACE_CHARS);
    if (objectBegin == std::string::npos || objectEnd == std::string::npos || objectEnd <= objectBegin) {
        return false;
    }
    if (jsonContent[objectBegin] != JSON_OBJECT_BEGIN || jsonContent[objectEnd] != JSON_OBJECT_END) {
        return false;
    }
    bodyBegin = objectBegin + 1;
    bodyEnd = objectEnd;
    return true;
}

std::shared_ptr<StageAssetProvider> StageAssetProvider::GetInstance()
{
    if (instance_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instance_ == nullptr) {
            instance_ = std::make_shared<StageAssetProvider>();
        }
    }

    return instance_;
}

std::vector<uint8_t> StageAssetProvider::GetPkgJsonBuffer(const std::string& moduleName)
{
    std::lock_guard<std::mutex> lock(providerLock_);
    std::vector<uint8_t> buffer;
    std::vector<std::string> pkgJsonFileList = StageAssetManager::GetInstance().GetPkgJsonFileList();
    LOGI("GetPkgJsonBuffer moduleName:%{public}s", moduleName.c_str());
    if (pkgJsonFileList.empty() || moduleIsUpdates_[moduleName]) {
        std::string pkgContextInfoJson = "pkgContextInfo.json";
        auto path = GetAppDataModuleDir() + "/" + moduleName;
        std::vector<std::string> fileFullPaths;
        GetAppDataModuleAssetList(path, fileFullPaths, false);
        for (auto& filePath : fileFullPaths) {
            if (Contains(filePath, "/" + moduleName + "/") && Contains(filePath, pkgContextInfoJson)) {
                LOGI("GetPkgJsonBuffer path:%{public}s", filePath.c_str());
                return ReadFileToVector(filePath);
            }
        }
    }
    for (const auto& pkgJsonPath : pkgJsonFileList) {
        if (Contains(pkgJsonPath, "/" + moduleName + "/")) {
            LOGI("GetPkgJsonBuffer pkgJsonPath:%{public}s", pkgJsonPath.c_str());
            buffer = ReadFileToVector(pkgJsonPath);
            if (buffer.empty()) {
                LOGE("pathData is null");
                break;
            }
            return buffer;
        }
    }
    return buffer;
}

std::pair<std::string, std::vector<uint8_t>> StageAssetProvider::GetPkgPairByAppDataPath(const std::string& moduleName)
{
    std::pair<std::string, std::vector<uint8_t>> result = { "", {} };
    auto appDataRootDir = GetAppDataModuleDir();
    if (appDataRootDir.empty()) {
        return result;
    }

    auto pkgJsonPath = appDataRootDir + "/" + moduleName + "/" + "pkgContextInfo.json";
    auto pkgJsonBuffer = ReadFileToVector(pkgJsonPath);
    if (pkgJsonBuffer.empty()) {
        LOGE("pkgJsonBuffer is empty");
        return result;
    }

    auto moduleJsonPath = appDataRootDir + "/" + moduleName + "/" + "module.json";
    auto moduleJsonBuffer = ReadFileToVector(moduleJsonPath);
    if (moduleJsonBuffer.empty()) {
        LOGE("The moduleJsonPath is not exist");
        return result;
    }
    moduleJsonBuffer.push_back('\0');
    std::string packageName;
    if (!ParseSharedModulePackageName(moduleName, moduleJsonBuffer, packageName)) {
        return result;
    }

    result.first = packageName;
    result.second = std::move(pkgJsonBuffer);
    return result;
}

bool StageAssetProvider::ParseSharedModulePackageName(
    const std::string& moduleName, const std::vector<uint8_t>& moduleJsonBuffer, std::string& packageName)
{
    packageName.clear();
    if (moduleName.empty() || moduleJsonBuffer.empty()) {
        return false;
    }

    nlohmann::json moduleJson = nlohmann::json::parse(moduleJsonBuffer.data(), nullptr, false);
    if (moduleJson.is_discarded()) {
        return false;
    }

    auto it = versionCodes_.find(moduleName);
    if (it != versionCodes_.end()) {
        int32_t versionCode = 0;
        if (moduleJson.contains("app") && moduleJson["app"].contains("versionCode")) {
            versionCode = moduleJson["app"]["versionCode"].get<int>();
        }
        if (versionCode < it->second) {
            return false;
        }
    }

    if (moduleJson.contains("module") && moduleJson["module"].contains("packageName")) {
        packageName = moduleJson["module"]["packageName"].get<std::string>();
    }
    return !packageName.empty();
}

std::string ExtractConfigurationFileName(const nlohmann::json& moduleJson)
{
    if (moduleJson.is_null() || moduleJson.is_discarded()) {
        return "";
    }
    auto appValue = moduleJson["app"];
    if (!appValue.is_object() || !appValue.contains("configuration")) {
        return "";
    }
    auto configurationValue = appValue["configuration"];
    std::string configurationStr = configurationValue.get<std::string>();
    auto delimiterPos = configurationStr.find(':');
    if (delimiterPos != std::string::npos) {
        return configurationStr.substr(delimiterPos + 1) + ".json";
    }
    return "";
}

std::list<std::vector<uint8_t>> StageAssetProvider::GetModuleJsonBufferList()
{
    printf("%s", __func__);
    std::lock_guard<std::mutex> lock(providerLock_);
    std::list<std::vector<uint8_t>> bufferList;

    std::vector<std::string> moduleJsonFileList = StageAssetManager::GetInstance().GetModuleJsonFileList();
    if (moduleJsonFileList.empty()) {
        printf("%s moduleJsonFileList count 0", __func__);
        return bufferList;
    }
    for (const auto& moduleJsonPath : moduleJsonFileList) {
        std::vector<uint8_t> pathData = ReadFileToVector(moduleJsonPath);
        pathData = StageAssetManager::GetInstance().UpdateModuleNameWithJsonData(pathData, moduleJsonPath);
        if (pathData.empty()) {
            continue;
        }
        bufferList.emplace_back(pathData);
    }
    if (!bufferList.empty()) {
        auto firstModule = bufferList.front();
        std::string moduleContent(firstModule.begin(), firstModule.end());
        nlohmann::json moduleJson = nlohmann::json::parse(moduleContent, nullptr, false);
        if (moduleJson.is_discarded()) {
            return bufferList;
        }
        fontConfigName_ = ExtractConfigurationFileName(moduleJson);
    }
    return bufferList;
}

std::vector<uint8_t> StageAssetProvider::GetFontConfigJsonBuffer(const std::string& moduleName)
{
    std::lock_guard<std::mutex> lock(providerLock_);
    if (fontConfigName_.empty()) {
        return {};
    }
    std::vector<std::string> pkgJsonFileList = StageAssetManager::GetInstance().GetAssetAllFilePathList();
    for (const auto& pkgJsonPath : pkgJsonFileList) {
        if (Contains(pkgJsonPath, "/" + fontConfigName_)) {
            auto buffer = ReadFileToVector(pkgJsonPath);
            if (buffer.empty()) {
                LOGE("pathData is null");
                break;
            }
            return buffer;
        }
    }
    return {};
}

std::vector<uint8_t> StageAssetProvider::GetModuleBuffer(
    const std::string& moduleName, std::string& modulePath, bool esmodule)
{
    printf("%s, moduleName : %s, modulePath : %s", __func__, moduleName.c_str(), modulePath.c_str());
    std::lock_guard<std::mutex> lock(providerLock_);
    std::vector<uint8_t> buffer;

    if (moduleName.empty()) {
        printf("%s, moduleName null", __func__);
        return buffer;
    }

    std::string abilityStageAbcPath =
        StageAssetManager::GetInstance().GetAbilityStageABC(moduleName, modulePath, esmodule);
    if (abilityStageAbcPath.empty() || moduleIsUpdates_[moduleName]) {
        printf("%s, abilityStageAbcPath null", __func__);

        std::string fullAbilityName = esmodule ? "modules.abc" : "AbilityStage.abc";
        auto path = GetAppDataModuleDir() + "/" + moduleName;
        std::vector<std::string> fileFullPaths;
        GetAppDataModuleAssetList(path, fileFullPaths, false);
        for (auto& file : fileFullPaths) {
            if (Contains(file, "/" + moduleName + "/") && Contains(file, fullAbilityName)) {
                modulePath = file;
                break;
            }
        }
        buffer = ReadFileToVector(modulePath);
        return buffer;
    }
    buffer = ReadFileToVector(abilityStageAbcPath);
    return buffer;
}

void StageAssetProvider::GetResIndexPath(
    const std::string& moduleName, std::string& appResIndexPath, std::string& sysResIndexPath)
{
    printf("%s, moduleName : %s", __func__, moduleName.c_str());
    std::lock_guard<std::mutex> lock(providerLock_);
    if (moduleName.empty()) {
        printf("%s, moduleName null", __func__);
        return;
    }
    auto it = staticResCache.find(moduleName);
    if (it != staticResCache.end() && !moduleIsUpdates_[moduleName]) {
        appResIndexPath = it->second.first;
        sysResIndexPath = it->second.second;
        return;
    }
    StageAssetManager::GetInstance().GetModuleResources(moduleName, appResIndexPath, sysResIndexPath);

    if (appResIndexPath.empty() || moduleIsUpdates_[moduleName]) {
        auto path = GetAppDataModuleDir() + "/" + moduleName;
        std::vector<std::string> fileFullPaths;
        GetAppDataModuleAssetList(path, fileFullPaths, false);
        for (auto& file : fileFullPaths) {
            if (Contains(file, "/" + moduleName + "/resources.index")) {
                appResIndexPath = file;
                continue;
            }
            if (sysResIndexPath.empty() && Contains(file, "/systemres/resources.index")) {
                sysResIndexPath = file;
                continue;
            }
        }
        staticResCache[moduleName] = { appResIndexPath, sysResIndexPath };
    }
    if (sysResIndexPath.empty() || moduleIsUpdates_[moduleName]) {
        auto path = GetAppDataModuleDir() + "/" + "systemres";
        std::vector<std::string> fileFullPaths;
        GetAppDataModuleAssetList(path, fileFullPaths, false);
        for (auto& file : fileFullPaths) {
            if (Contains(file, "/systemres/resources.index")) {
                sysResIndexPath = file;
                continue;
            }
        }
        staticResCache[moduleName] = { appResIndexPath, sysResIndexPath };
    }
}

std::vector<uint8_t> StageAssetProvider::GetModuleAbilityBuffer(
    const std::string& moduleName, const std::string& abilityName, std::string& modulePath, bool esmodule)
{
    printf("%s, moduleName : %s, abilityName : %s", __func__, moduleName.c_str(), abilityName.c_str());
    std::lock_guard<std::mutex> lock(providerLock_);
    std::vector<uint8_t> buffer;

    if (moduleName.empty()) {
        printf("%s, moduleName null", __func__);
        return buffer;
    }

    if (abilityName.empty()) {
        printf("%s, abilityName null", __func__);
        buffer = GetModuleBuffer(moduleName, modulePath, esmodule);
        return buffer;
    }

    std::string moduleAbilityPath =
        StageAssetManager::GetInstance().GetModuleAbilityABC(moduleName, abilityName, modulePath, esmodule);
    if (moduleAbilityPath.empty() || moduleIsUpdates_[moduleName]) {
        printf("%s, moduleAbilityPath null", __func__);

        std::string fullAbilityName = esmodule ? "modules.abc" : abilityName + ".abc";
        auto path = GetAppDataModuleDir() + "/" + moduleName;
        std::vector<std::string> fileFullPaths;
        GetAppDataModuleAssetList(path, fileFullPaths, false);
        for (auto& file : fileFullPaths) {
            if (Contains(file, "/" + moduleName + "/") && Contains(file, fullAbilityName)) {
                modulePath = file;
                break;
            }
        }
        buffer = ReadFileToVector(modulePath);
        return buffer;
    }
    buffer = ReadFileToVector(moduleAbilityPath);
    return buffer;
}

std::string StageAssetProvider::GetBundleCodeDir()
{
    return StageAssetManager::GetInstance().GetBundlePath();
}

std::string StageAssetProvider::GetCacheDir()
{
    const char* xdgCache = getenv("XDG_CACHE_HOME");
    std::string base;
    if (xdgCache != nullptr && xdgCache[0] != '\0') {
        base = xdgCache;
    } else {
        const char* home = getenv("HOME");
        base = std::string(home != nullptr ? home : ".") + "/.cache";
    }
    return base + "/arkui-x";
}

std::string StageAssetProvider::GetResourceFilePrefixPath()
{
    return StageAssetManager::GetInstance().GetResourceFilePrefixPath();
}

std::string StageAssetProvider::GetTempDir()
{
    const char* tmp = getenv("TMPDIR");
    if (tmp != nullptr && tmp[0] != '\0') {
        std::string tempDir(tmp);
        while (tempDir.size() > 1 && tempDir.back() == '/') {
            tempDir.pop_back();
        }
        return tempDir;
    }
    return "/tmp";
}

std::string StageAssetProvider::GetFilesDir()
{
    return GetDocumentsDirectory() + "/files";
}

std::string StageAssetProvider::GetDatabaseDir()
{
    // Mirror the macOS port, which returned the documents root here (not the
    // database subdir).
    return GetDocumentsDirectory();
}

std::string StageAssetProvider::GetPreferencesDir()
{
    const char* xdgConfig = getenv("XDG_CONFIG_HOME");
    std::string base;
    if (xdgConfig != nullptr && xdgConfig[0] != '\0') {
        base = xdgConfig;
    } else {
        const char* home = getenv("HOME");
        base = std::string(home != nullptr ? home : ".") + "/.config";
    }
    return base + "/arkui-x";
}

std::string StageAssetProvider::GetResourceDir(const std::string& moduleName) const
{
    if (moduleName.empty()) {
        LOGW("GetResourceDir failed, moduleName is empty");
        return "";
    }
    std::string resourceDirPath = GetExecutableDir() + "/arkui-x/" + moduleName + "/resources/resfile";
    std::error_code ec;
    if (!std::filesystem::exists(resourceDirPath, ec) || ec) {
        LOGW("Resource dir not found: %{public}s", resourceDirPath.c_str());
        return "";
    }
    return resourceDirPath;
}

std::string StageAssetProvider::GetAppDataModuleDir() const
{
    return GetDocumentsDirectory() + "/files/arkui-x";
}

std::vector<std::string> StageAssetProvider::GetAllFilePath()
{
    return StageAssetManager::GetInstance().GetAssetAllFilePathList();
}

bool StageAssetProvider::GetAppDataModuleAssetList(
    const std::string& path, std::vector<std::string>& fileFullPaths, bool onlyChild)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec || !std::filesystem::is_directory(path, ec)) {
        return false;
    }
    bool found = false;
    // onlyChild no use (mirrors the macOS recursive subpaths walk)
    for (auto it = std::filesystem::recursive_directory_iterator(
             path, std::filesystem::directory_options::skip_permission_denied, ec);
         it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) {
            break;
        }
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && !fileEc) {
            fileFullPaths.emplace_back(it->path().string());
            found = true;
        }
    }
    return found;
}

std::vector<uint8_t> StageAssetProvider::GetBufferByAppDataPath(const std::string& fileFullPath)
{
    return ReadFileToVector(fileFullPath);
}

std::vector<uint8_t> StageAssetProvider::GetAotBuffer(const std::string& fileName)
{
    std::vector<uint8_t> buffer;
    return buffer;
}

bool ExistDir(const std::string& target)
{
    std::error_code ec;
    return std::filesystem::is_directory(target, ec) && !ec;
}

void StageAssetProvider::SetBundleName(const std::string& bundleName)
{
    bundleName_ = bundleName;
}

std::string StageAssetProvider::GetSplicingModuleName(const std::string& moduleName)
{
    if (moduleName.empty() || bundleName_.empty()) {
        return moduleName;
    }
    std::string fullModuleName = bundleName_ + "." + moduleName;
    std::string modulePath = GetAppDataModuleDir() + '/' + fullModuleName;
    return ExistDir(modulePath) ? fullModuleName : moduleName;
}

void StageAssetProvider::InitModuleVersionCode()
{
    auto moduleList = GetModuleJsonBufferList();
    std::string bundleName = "";
    std::string moduleName = "";
    int32_t versionCode = 0;
    for (auto& buffer : moduleList) {
        buffer.push_back('\0');
        nlohmann::json moduleJson = nlohmann::json::parse(buffer.data(), nullptr, false);
        if (moduleJson.is_discarded()) {
            continue;
        }
        if (moduleJson.contains("app") && moduleJson["app"].contains("versionCode")) {
            versionCode = moduleJson["app"]["versionCode"].get<int>();
        }
        if (moduleJson.contains("module") && moduleJson["module"].contains("name")) {
            moduleName = moduleJson["module"]["name"].get<std::string>();
        }
        if (moduleJson.contains("app") && moduleJson["app"].contains("bundleName")) {
            bundleName = moduleJson["app"]["bundleName"].get<std::string>();
        }
        if (!moduleName.empty() && !bundleName.empty()) {
            std::string fullModuleName = bundleName + "." + moduleName;
            std::string modulePath = GetAppDataModuleDir() + '/' + fullModuleName;
            moduleName = ExistDir(modulePath) ? fullModuleName : moduleName;
        }
        if (!moduleName.empty() && versionCode > 0) {
            versionCodes_.emplace(moduleName, versionCode);
        }
    }
}

void StageAssetProvider::UpdateVersionCode(const std::string& moduleName, bool needUpdate)
{
    bool isUpdate = false;
    if (needUpdate) {
        auto modulePath = GetAppDataModuleDir() + '/' + moduleName + "/module.json";
        auto dynamicModuleBuffer = GetBufferByAppDataPath(modulePath);
        dynamicModuleBuffer.push_back('\0');
        int32_t versionCode = 0;
        nlohmann::json moduleJson = nlohmann::json::parse(dynamicModuleBuffer.data(), nullptr, false);
        if (!moduleJson.is_discarded() && moduleJson.contains("app") && moduleJson["app"].contains("versionCode")) {
            versionCode = moduleJson["app"]["versionCode"].get<int>();
        }
        if (versionCode > 0) {
            auto it = versionCodes_.find(moduleName);
            if (it == versionCodes_.end() || it->second < versionCode) {
                isUpdate = true;
                versionCodes_[moduleName] = versionCode;
            }
        }
    }
    moduleIsUpdates_[moduleName] = isUpdate;
}

bool StageAssetProvider::IsDynamicUpdateModule(const std::string& moduleName)
{
    bool isDynamicUpdate = false;
    auto it = moduleIsUpdates_.find(moduleName);
    if (it != moduleIsUpdates_.end()) {
        isDynamicUpdate = it->second;
    }
    return isDynamicUpdate;
}

std::vector<std::string> StageAssetProvider::GetAllModuleDirectories()
{
    std::vector<std::string> moduleDirs;
    auto appDataDir = GetAppDataModuleDir();
    if (appDataDir.empty()) {
        LOGE("GetAllModuleDirectories: AppData module dir is empty");
        return moduleDirs;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(appDataDir, ec) || ec) {
        LOGW("GetAllModuleDirectories: Failed to read AppData dir: %{public}s", appDataDir.c_str());
        return moduleDirs;
    }

    for (const auto& entry : std::filesystem::directory_iterator(
             appDataDir, std::filesystem::directory_options::skip_permission_denied, ec)) {
        if (ec) {
            break;
        }
        std::error_code dirEc;
        if (!entry.is_directory(dirEc) || dirEc) {
            continue;
        }
        std::string modulePath = entry.path().string();
        std::error_code existEc;
        bool hasModuleJson = std::filesystem::exists(modulePath + "/module.json", existEc) && !existEc;
        bool hasPkgContextInfo = std::filesystem::exists(modulePath + "/pkgContextInfo.json", existEc) && !existEc;

        if (hasModuleJson && hasPkgContextInfo) {
            std::string moduleName = entry.path().filename().string();
            moduleDirs.emplace_back(moduleName);
            LOGI("GetAllModuleDirectories: Valid module: %{public}s", moduleName.c_str());
        }
    }
    return moduleDirs;
}

bool StageAssetProvider::GetSourceMapBufferByModuleName(const std::string& moduleName, std::string& content)
{
    if (moduleName.empty()) {
        LOGW("GetSourceMapBufferByModuleName: moduleName is empty");
        return false;
    }
    if (!IsValidSourceMapModuleName(moduleName)) {
        LOGW("GetSourceMapBufferByModuleName: moduleName is invalid");
        return false;
    }

    if (GetSourceMapBufferFromSandbox(moduleName, content)) {
        LOGI("GetSourceMapBufferByModuleName: from sandbox moduleName=%{public}s, size=%{public}zu", moduleName.c_str(),
            content.size());
        return true;
    }
    if (GetSourceMapBufferFromBundle(moduleName, content)) {
        LOGI("GetSourceMapBufferByModuleName: from bundle moduleName=%{public}s, size=%{public}zu", moduleName.c_str(),
            content.size());
        return true;
    }
    LOGE("GetSourceMapBufferByModuleName: no sourceMaps.map found, module: %{public}s", moduleName.c_str());
    return false;
}

bool StageAssetProvider::GetSourceMapBufferFromSandbox(const std::string& moduleName, std::string& content)
{
    std::string targetPath = moduleName + PATH_SEPARATOR + SOURCE_MAP_RELATIVE_PATH;
    auto sandboxPath = GetAppDataModuleDir() + PATH_SEPARATOR + moduleName;
    std::vector<std::string> sandboxFilePaths;
    GetAppDataModuleAssetList(sandboxPath, sandboxFilePaths, false);
    std::vector<std::string> sourceMapPaths;
    for (const auto& filePath : sandboxFilePaths) {
        if (filePath.find(targetPath) != std::string::npos) {
            sourceMapPaths.emplace_back(filePath);
        }
    }
    if (sourceMapPaths.empty()) {
        return false;
    }
    return MergeSourceMapFilesFromSandbox(sourceMapPaths, content);
}

bool StageAssetProvider::GetSourceMapBufferFromBundle(const std::string& moduleName, std::string& content)
{
    std::vector<std::string> allPaths = GetAllFilePath();
    std::string targetPath = moduleName + PATH_SEPARATOR + SOURCE_MAP_RELATIVE_PATH;
    std::vector<std::string> sourceMapPaths;
    for (const auto& filePath : allPaths) {
        if (filePath.find(targetPath) != std::string::npos) {
            sourceMapPaths.emplace_back(filePath);
        }
    }
    if (sourceMapPaths.empty()) {
        return false;
    }
    return MergeSourceMapFilesFromBundle(sourceMapPaths, content);
}

bool StageAssetProvider::MergeSourceMapFilesFromSandbox(
    const std::vector<std::string>& sourceMapPaths, std::string& content)
{
    if (sourceMapPaths.size() == 1) {
        auto buffer = GetBufferByAppDataPath(sourceMapPaths[0]);
        if (buffer.empty()) {
            return false;
        }
        content.assign(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        return true;
    }

    std::vector<std::string> parts;
    for (const auto& sourceMapPath : sourceMapPaths) {
        auto buffer = GetBufferByAppDataPath(sourceMapPath);
        if (buffer.empty()) {
            continue;
        }
        parts.emplace_back(reinterpret_cast<const char*>(buffer.data()), buffer.size());
    }
    if (parts.empty()) {
        return false;
    }
    content = MergeSourceMapJsonParts(parts);
    return !content.empty();
}

bool StageAssetProvider::MergeSourceMapFilesFromBundle(
    const std::vector<std::string>& sourceMapPaths, std::string& content)
{
    if (sourceMapPaths.size() == 1) {
        return ReadSourceMapFile(sourceMapPaths[0], content);
    }

    std::vector<std::string> parts;
    for (const auto& sourceMapPath : sourceMapPaths) {
        std::string moduleContent;
        if (!ReadSourceMapFile(sourceMapPath, moduleContent)) {
            continue;
        }
        parts.emplace_back(std::move(moduleContent));
    }
    if (parts.empty()) {
        return false;
    }
    content = MergeSourceMapJsonParts(parts);
    return !content.empty();
}

std::string StageAssetProvider::MergeSourceMapJsonParts(const std::vector<std::string>& parts)
{
    std::string mergedSourceMap;
    size_t mergedSourceMapSize = JSON_OBJECT_BOUNDARY_LENGTH + parts.size();
    for (const auto& part : parts) {
        mergedSourceMapSize += part.size();
    }
    mergedSourceMap.reserve(mergedSourceMapSize);
    mergedSourceMap.push_back(JSON_OBJECT_BEGIN);
    for (const auto& part : parts) {
        size_t bodyBegin = 0;
        size_t bodyEnd = 0;
        if (!GetJsonObjectBodyRange(part, bodyBegin, bodyEnd)) {
            continue;
        }
        auto memberBegin = part.find_first_not_of(JSON_WHITESPACE_CHARS, bodyBegin);
        if (memberBegin == std::string::npos || memberBegin >= bodyEnd) {
            continue;
        }
        auto memberEnd = part.find_last_not_of(JSON_WHITESPACE_CHARS, bodyEnd - 1);
        if (memberEnd == std::string::npos || memberEnd < memberBegin) {
            continue;
        }
        if (mergedSourceMap.back() != JSON_OBJECT_BEGIN) {
            mergedSourceMap.push_back(JSON_MEMBER_SEPARATOR);
        }
        mergedSourceMap.append(part, memberBegin, memberEnd - memberBegin + 1);
    }
    mergedSourceMap.push_back(JSON_OBJECT_END);
    return mergedSourceMap.size() > JSON_OBJECT_BOUNDARY_LENGTH ? mergedSourceMap : "";
}

bool StageAssetProvider::ReadSourceMapFile(const std::string& sourceMapPath, std::string& content)
{
    if (sourceMapPath.empty()) {
        return false;
    }
    std::string fullPath = sourceMapPath;
    if (sourceMapPath[0] != '/') {
        fullPath = GetExecutableDir() + "/" + sourceMapPath;
    }
    auto data = ReadFileToVector(fullPath);
    if (data.empty()) {
        return false;
    }
    content = std::string(reinterpret_cast<const char*>(data.data()), data.size());
    return true;
}
} // namespace Platform
} // namespace AbilityRuntime
} // namespace OHOS
