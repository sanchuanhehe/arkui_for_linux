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

// Linux/Wayland native port of the macOS Objective-C StageAssetManager. The
// original was an NSObject singleton scanning [NSBundle mainBundle].bundlePath
// with NSFileManager. The portable C++ form scans the directory next to the
// executable (see resource_path_util.cpp) with std::filesystem and keeps the
// collected paths in std::vector<std::string>.

#ifndef FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_ASSET_MANAGER_H
#define FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_ASSET_MANAGER_H

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

class StageAssetManager {
public:
    static StageAssetManager& GetInstance();

    void ModuleFilesWithBundleDirectory(const std::string& bundleDirectory);

    void LaunchAbility(bool isLoadArkUI);

    bool IsExistFileForPath(const std::string& filePath);

    std::vector<uint8_t> UpdateModuleNameWithJsonData(
        const std::vector<uint8_t>& data, const std::string& moduleJsonPath);

    std::string GetBundlePath();

    std::vector<std::string> GetAssetAllFilePathList();

    std::string GetResourceFilePrefixPath();

    std::vector<std::string> GetPkgJsonFileList();

    std::vector<std::string> GetModuleJsonFileList();

    std::string GetAbilityStageABC(const std::string& moduleName, std::string& modulePath, bool esmodule);

    void GetModuleResources(
        const std::string& moduleName, std::string& appResIndexPath, std::string& sysResIndexPath);

    std::string GetModuleAbilityABC(
        const std::string& moduleName, const std::string& abilityName, std::string& modulePath, bool esmodule);

private:
    StageAssetManager() = default;
    ~StageAssetManager() = default;

    bool CreateDocumentSubDirectory(const std::string& path);

    std::mutex mutex_;
    std::vector<std::string> allModuleFilePathArray_;
    std::vector<std::string> moduleJsonFileArray_;
    std::vector<std::string> pkgJsonFileArray_;
    std::string bundlePath_;
};

#endif // FOUNDATION_ACE_ADAPTER_LINUX_STAGE_ABILITY_ASSET_MANAGER_H
