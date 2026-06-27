/*
 * Copyright (c) 2023 Huawei Device Co., Ltd.
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

// Linux/Wayland native port: the macOS version read NSProcessInfo /
// [NSBundle mainBundle].infoDictionary["CFBundleName"]. On Linux there is no app
// bundle: the pid comes from getpid() and the process name from
// /proc/self/comm, which doubles as the bundle name.

#include "application_context_adapter.h"

#include <fstream>
#include <string>
#include <unistd.h>

namespace OHOS {
namespace AbilityRuntime {
namespace Platform {
namespace {
std::string GetProcessName()
{
    std::ifstream comm("/proc/self/comm");
    std::string name;
    if (comm.is_open()) {
        std::getline(comm, name);
    }
    // strip trailing newline if any
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) {
        name.pop_back();
    }
    return name;
}
} // namespace

std::shared_ptr<ApplicationContextAdapter> ApplicationContextAdapter::instance_ = nullptr;
std::mutex ApplicationContextAdapter::mutex_;

std::shared_ptr<ApplicationContextAdapter> ApplicationContextAdapter::GetInstance()
{
    if (instance_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instance_ == nullptr) {
            instance_ = std::make_shared<ApplicationContextAdapter>();
        }
    }

    return instance_;
}

std::vector<RunningProcessInfo> ApplicationContextAdapter::GetRunningProcessInformation()
{
    RunningProcessInfo info;
    info.pid = getpid();
    std::string processName = GetProcessName();
    info.processName = processName;
    info.bundleNames.emplace_back(processName);
    std::vector<RunningProcessInfo> infos { info };
    return infos;
}
} // namespace Platform
} // namespace AbilityRuntime
} // namespace OHOS
