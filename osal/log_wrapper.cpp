/*
 * Copyright (c) 2022-2025 Huawei Device Co., Ltd.
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

#include "base/log/log_wrapper.h"

#ifdef _GNU_SOURCE
#include <dlfcn.h>
#endif
#include <mutex>

#include <thread>

#include "securec.h"

#ifdef ACE_INSTANCE_LOG
#include "core/common/container.h"
#endif

#include <cstdio>
#include "vsnprintf_s_p.h"

namespace OHOS::Ace {
// Phase 0 path X (M1 link): g_DOMAIN_CONTENTS_MAP (used by every ACE log macro
// via .at(tag)) is defined in the OHOS osal log_wrapper but not the macOS one;
// an empty/missing map would throw on .at(). Copy the real table.
const std::unordered_map<AceLogTag, const char*> g_DOMAIN_CONTENTS_MAP = {
    { AceLogTag::ACE_DEFAULT_DOMAIN, "Ace" },
    { AceLogTag::ACE_ALPHABET_INDEXER, "AceAlphabetIndexer" },
    { AceLogTag::ACE_COUNTER, "AceCounter" },
    { AceLogTag::ACE_SUB_WINDOW, "AceSubWindow" },
    { AceLogTag::ACE_FORM, "AceForm" },
    { AceLogTag::ACE_DRAG, "AceDrag" },
    { AceLogTag::ACE_VIDEO, "AceVideo" },
    { AceLogTag::ACE_COMPONENT_SNAPSHOT, "AceComponentSnapshot" },
    { AceLogTag::ACE_CANVAS, "AceCanvas" },
    { AceLogTag::ACE_REFRESH, "AceRefresh" },
    { AceLogTag::ACE_SCROLL, "AceScroll" },
    { AceLogTag::ACE_SCROLLABLE, "AceScrollable" },
    { AceLogTag::ACE_FONT, "AceFont" },
    { AceLogTag::ACE_OVERLAY, "AceOverlay" },
    { AceLogTag::ACE_DIALOG_TIMEPICKER, "AceDialogTimePicker" },
    { AceLogTag::ACE_DIALOG, "AceDialog" },
    { AceLogTag::ACE_PANEL, "AcePanel" },
    { AceLogTag::ACE_MENU, "AceMenu" },
    { AceLogTag::ACE_TEXTINPUT, "AceTextInput" },
    { AceLogTag::ACE_TEXT, "AceText" },
    { AceLogTag::ACE_TEXT_FIELD, "AceTextField" },
    { AceLogTag::ACE_SWIPER, "AceSwiper" },
    { AceLogTag::ACE_TABS, "AceTabs" },
    { AceLogTag::ACE_SAFE_AREA, "AceSafeArea" },
    { AceLogTag::ACE_GRIDROW, "AceGridRow" },
    { AceLogTag::ACE_INPUTTRACKING, "AceInputTracking" },
    { AceLogTag::ACE_RICH_TEXT, "AceRichText" },
    { AceLogTag::ACE_WEB, "AceWeb" },
    { AceLogTag::ACE_FOCUS, "AceFocus" },
    { AceLogTag::ACE_MOUSE, "AceMouse" },
    { AceLogTag::ACE_GESTURE, "AceGesture" },
    { AceLogTag::ACE_IMAGE, "AceImage" },
    { AceLogTag::ACE_RATING, "AceRating" },
    { AceLogTag::ACE_LIST, "AceList" },
    { AceLogTag::ACE_NAVIGATION, "AceNavigation" },
    { AceLogTag::ACE_WATERFLOW, "AceWaterFlow" },
    { AceLogTag::ACE_ACCESSIBILITY, "AceAccessibility" },
    { AceLogTag::ACE_ROUTER, "AceRouter" },
    { AceLogTag::ACE_THEME, "AceTheme" },
    { AceLogTag::ACE_BORDER_IMAGE, "AceBorderImage" },
    { AceLogTag::ACE_GRID, "AceGrid" },
    { AceLogTag::ACE_PLUGIN_COMPONENT, "AcePluginComponent" },
    { AceLogTag::ACE_UIEXTENSIONCOMPONENT, "AceUiExtensionComponent" },
    { AceLogTag::ACE_IF, "AceIf" },
    { AceLogTag::ACE_FOREACH, "AceForEach" },
    { AceLogTag::ACE_LAZY_FOREACH, "AceLazyForEach" },
    { AceLogTag::ACE_GAUGE, "AceGauge" },
    { AceLogTag::ACE_HYPERLINK, "AceHyperLink" },
    { AceLogTag::ACE_ANIMATION, "AceAnimation" },
    { AceLogTag::ACE_XCOMPONENT, "AceXcomponent" },
    { AceLogTag::ACE_AUTO_FILL, "AceAutoFill" },
    { AceLogTag::ACE_KEYBOARD, "AceKeyboard" },
    { AceLogTag::ACE_UIEVENT, "AceUIEvent" },
    { AceLogTag::ACE_UI_SERVICE, "AceUIService" },
    { AceLogTag::ACE_DISPLAY_SYNC, "AceDisplaySync" },
    { AceLogTag::ACE_RESOURCE, "AceResource" },
    { AceLogTag::ACE_SIDEBAR, "AceSideBarContainer" },
    { AceLogTag::ACE_GEOMETRY_TRANSITION, "AceGeometryTransition" },
    { AceLogTag::ACE_DOWNLOAD_MANAGER, "DownloadManager" },
    { AceLogTag::ACE_WINDOW_SCENE, "AceWindowScene" },
    { AceLogTag::ACE_NODE_CONTAINER, "AceNodeContainer" },
    { AceLogTag::ACE_NATIVE_NODE, "AceNativeNode" },
    { AceLogTag::ACE_ISOLATED_COMPONENT, "AceIsolatedComponent" },
    { AceLogTag::ACE_DYNAMIC_COMPONENT, "AceDynamicComponent" },
    { AceLogTag::ACE_SECURITYUIEXTENSION, "AceSecurityUiExtensionComponent" },
    { AceLogTag::ACE_MARQUEE, "AceMarquee" },
    { AceLogTag::ACE_OBSERVER, "AceObserver" },
    { AceLogTag::ACE_EMBEDDED_COMPONENT, "AceEmbeddedComponent" },
    { AceLogTag::ACE_TEXT_CLOCK, "AceTextClock" },
    { AceLogTag::ACE_FOLDER_STACK, "AceFolderStack" },
    { AceLogTag::ACE_SELECT_COMPONENT, "AceSelectComponent" },
    { AceLogTag::ACE_STATE_STYLE, "AceStateStyle" },
    { AceLogTag::ACE_SEARCH, "AceSearch" },
    { AceLogTag::ACE_STATE_MGMT, "AceStateMgmt" },
    { AceLogTag::ACE_REPEAT, "AceRepeat" },
    { AceLogTag::ACE_SHEET, "AceSheet" },
    { AceLogTag::ACE_CANVAS_COMPONENT, "AceCanvasComponent" },
    { AceLogTag::ACE_SCROLL_BAR, "AceScrollBar" },
    { AceLogTag::ACE_MOVING_PHOTO, "AceMovingPhoto" },
    { AceLogTag::ACE_ARK_COMPONENT, "AceArkComponent" },
    { AceLogTag::ACE_WINDOW, "AceWindow" },
    { AceLogTag::ACE_WINDOW_PIPELINE, "AceWindowPipeline" },
    { AceLogTag::ACE_INPUTKEYFLOW, "InputKeyFlow" },
    { AceLogTag::ACE_APPBAR, "AceAppBar" },
    { AceLogTag::ACE_SELECT_OVERLAY, "AceSelectOverlay" },
    { AceLogTag::ACE_CLIPBOARD, "AceClipBoard" },
    { AceLogTag::ACE_VISUAL_EFFECT, "AceVisualEffect" },
    { AceLogTag::ACE_SECURITY_COMPONENT, "AceSecurityComponent" },
    { AceLogTag::ACE_MEDIA_QUERY, "AceMediaQuery" },
    { AceLogTag::ACE_LAYOUT_INSPECTOR, "AceLayoutInspector" },
    { AceLogTag::ACE_LAYOUT, "AceLayout" },
    { AceLogTag::ACE_STYLUS, "AceStylus" },
    { AceLogTag::ACE_BADGE, "AceBadge" },
    { AceLogTag::ACE_QRCODE, "AceQRCode" },
    { AceLogTag::ACE_PROGRESS, "ACE_PROGRESS" },
    { AceLogTag::ACE_DRAWABLE_DESCRIPTOR, "AceDrawableDescriptor" },
    { AceLogTag::ACE_LAZY_GRID, "AceLazyGrid" },
    { AceLogTag::ACE_CONTAINER_PICKER, "AceContainerPicker" },
    { AceLogTag::ACE_COLOR_SAMPLER, "AceColorSampler" },
    { AceLogTag::ACE_DEPTH_COMPONENT, "AceDepthComponent" },
    { AceLogTag::ACE_LAZY_COLUMN, "AceLazyColumn" },
    { AceLogTag::ACE_LAZY_WATER_FLOW, "AceLazyWaterFlow" },
};

namespace {

constexpr uint32_t MAX_BUFFER_SIZE = 4000; // MAX_BUFFER_SIZE same with hilog
constexpr uint32_t MAX_TIME_SIZE = 32;
const char* const LOGLEVELNAME[] = { "DEBUG", "INFO", "WARNING", "ERROR", "FATAL" };

static void StripFormatString(const std::string& prefix, std::string& str)
{
    for (auto pos = str.find(prefix, 0); pos != std::string::npos; pos = str.find(prefix, pos)) {
        str.erase(pos, prefix.size());
    }
}

const char* LOG_TAGS[] = {
    "Ace",
    "Console",
};

#ifdef ACE_INSTANCE_LOG
constexpr const char* INSTANCE_ID_GEN_REASONS[] = {
    "scope",
    "active",
    "default",
    "singleton",
    "foreground",
    "undefined",
};
#endif

} // namespace

// initial static member object
LogLevel LogWrapper::level_ = LogLevel::DEBUG;

const char* GetNameForLogLevel(LogLevel level)
{
    if (level <= LogLevel::FATAL) {
        return LOGLEVELNAME[static_cast<int>(level)];
    }
    return "UNKNOWN";
}

char LogWrapper::GetSeparatorCharacter()
{
    return '/';
}

std::string GetTimeStamp()
{
    time_t tt = time(nullptr);
    tm* t = localtime(&tt);
    char time[MAX_TIME_SIZE];

    if (sprintf_s(time, sizeof(time), " %02d/%02d %02d:%02d:%02d", t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min,
            t->tm_sec) < 0) {
        return std::string();
    }
    return std::string(time);
}

// [linux-port] Apple os_log_type_t table dropped; linux logs via fprintf(stderr).
    
// Phase 0 path X: with USE_HILOG defined (true for is_mac in ace_config.gni), the
// shared frameworks/base/log/log_wrapper.cpp drops the variadic PrintLog overload
// (#if !defined(USE_HILOG)). The macOS osal only implements the va_list overload,
// so the variadic symbol everyone calls (ACE_LOG macros) goes undefined at link.
// Provide it here, forwarding to the va_list version.
void LogWrapper::PrintLog(LogDomain domain, LogLevel level, AceLogTag tag, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    PrintLog(domain, level, tag, fmt, args);
    va_end(args);
}

void LogWrapper::PrintLog(LogDomain domain, LogLevel level, AceLogTag tag, const char* fmt, va_list args)
{
    if (!OHOS::Ace::LogWrapper::JudgeLevel(level)) {
        return;
    }

    char buffer[MAX_BUFFER_SIZE] = {0};
    int charsOut = vsnprintfp_s(buffer, MAX_BUFFER_SIZE, MAX_BUFFER_SIZE - 1, 1, fmt, args);
    if (charsOut < 0) {
        return;
    }

    // [linux-port] No Apple os_log / Objective-C delegate bridge on linux; emit
    // the formatted line to stderr.
    fprintf(stderr, "[%s][%s] %s\n", LOG_TAGS[static_cast<uint32_t>(domain)],
        GetNameForLogLevel(level), buffer);
}

#ifdef ACE_INSTANCE_LOG
int32_t LogWrapper::GetId()
{
    return Container::CurrentId();
}

const std::string LogWrapper::GetIdWithReason()
{
    int32_t currentId = ContainerScope::CurrentId();
    std::pair<int32_t, InstanceIdGenReason> idWithReason = ContainerScope::CurrentIdWithReason();
    return std::to_string(currentId) + ":" + std::to_string(idWithReason.first) + ":" +
           INSTANCE_ID_GEN_REASONS[static_cast<uint32_t>(idWithReason.second)];
}
#endif

bool LogBacktrace(size_t maxFrameNums)
{
    static const char* (*pfnGetTrace)(size_t, size_t);
#ifdef _GNU_SOURCE
    if (!pfnGetTrace) {
        pfnGetTrace = (decltype(pfnGetTrace))dlsym(RTLD_DEFAULT, "GetTrace");
    }
#endif
    if (!pfnGetTrace) {
        return false;
    }

    static std::mutex mtx;
    std::lock_guard lock(mtx);
    size_t skipFrameNum = 2;
    LOGI("Backtrace: skipFrameNum=%{public}zu maxFrameNums=%{public}zu\n%{public}s",
        skipFrameNum, maxFrameNums, pfnGetTrace(skipFrameNum, maxFrameNums));
    return true;
}

CallbackLogger::CallbackLogger(const std::string& funcName, uintptr_t callback) {}

CallbackLogger::~CallbackLogger() {}

} // namespace OHOS::Ace
