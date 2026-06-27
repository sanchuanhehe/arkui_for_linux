/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

// Linux/Wayland native port: the macOS version used NSBundle / NSSearchPath.
// On Linux there is no app bundle; the resources sit next to the executable in
// an "arkui-x" subdir (mirroring how the macos port lays out
// out/.../ace_engine/arkui-x), and the writable sandbox follows the XDG base
// dir spec ($XDG_DATA_HOME or ~/.local/share).

#include "resource_path_util.h"

#include <climits>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace OHOS::Ace {
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
} // namespace

std::string ResourcePathUtil::GetBundlePath()
{
    return GetExecutableDir() + "/arkui-x";
}

std::string ResourcePathUtil::GetSandboxPath()
{
    const char* xdgData = getenv("XDG_DATA_HOME");
    std::string base;
    if (xdgData != nullptr && xdgData[0] != '\0') {
        base = xdgData;
    } else {
        const char* home = getenv("HOME");
        base = std::string(home != nullptr ? home : ".") + "/.local/share";
    }
    return base + "/arkui-x/files";
}
} // namespace OHOS::Ace
