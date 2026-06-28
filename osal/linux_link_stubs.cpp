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

// Linux/Wayland native port (Route A, M1 link): centralized link stubs for the
// native-Linux app shell, mirroring adapter/macos/osal/mac_link_stubs.cpp. These
// cover non-rendering-path symbols referenced by libace but whose real impls are
// iOS/OHOS-only (or need deps not built for linux). They let `ace_linux` link and
// open a window; real implementations are follow-up work. Each stub no-ops /
// returns a default and is defined exactly once here.
//
// NOTE vs mac_link_stubs.cpp: the SkDebugf, LogInterfaceBridge log-forwarding and
// TextInputClientHandler/TextInputConnectionImpl stubs are intentionally omitted —
// on linux those symbols are already provided (skia's real SkDebugf, the linux
// osal log_wrapper, and adapter/linux/osal/input_method_manager_ios.cpp), so
// stubbing them here would be a duplicate definition.

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "base/log/log_wrapper.h"

// ---------------------------------------------------------------------------
// DynamicModule menu factories (extern "C", referenced by menu pattern bridge).
// ---------------------------------------------------------------------------
extern "C" void* OHOS_ACE_DynamicModule_Create_Menu()
{
    return nullptr;
}
extern "C" void* OHOS_ACE_DynamicModule_Create_MenuItem()
{
    return nullptr;
}
extern "C" void* OHOS_ACE_DynamicModule_Create_MenuItemGroup()
{
    return nullptr;
}

// ---------------------------------------------------------------------------
// Accessibility ObjC-bridge free functions (declared in
// adapter/linux/osal/accessibility_manager_impl.h). accessibility_manager_impl.cpp
// references these; on linux accessibility is not wired yet, so they are inert.
// ---------------------------------------------------------------------------
#include "adapter/linux/osal/accessibility_manager_impl.h"

using namespace OHOS::Ace::Framework;

bool ExecuteActionOC(
    const int /* windowId */, const std::shared_ptr<AccessibilityManagerImpl::InteractionOperation>& /* op */)
{
    return false;
}

void UpdateNodesOC(
    const std::list<OHOS::Accessibility::AccessibilityElementInfo>& /* infos */, const int /* windowId */,
    const size_t /* eventType */)
{
}

void SendAccessibilityEventOC(const int64_t /* elementId */, const int /* windowId */, const size_t /* eventType */)
{
}

bool SubscribeState(
    const int /* windowId */, const std::shared_ptr<AccessibilityManagerImpl::AccessibilityStateObserver>& /* obs */)
{
    return false;
}

void UnSubscribeState(const int /* windowId */)
{
}

void AnnounceForAccessibilityOC(const std::string& /* text */)
{
}

bool IsUITestingEnabled(const uint32_t /* windowId */)
{
    return false;
}

// ---------------------------------------------------------------------------
// ace platform services (non-startup paths): static helpers + concrete-class
// methods that have no linux implementation. Inert so libace links; these cover
// vibration, html<->spanstring, and multi-type clipboard records.
// ---------------------------------------------------------------------------
#include "core/common/vibrator/vibrator_utils.h"
// span_string.h provides the NG::SpanItem / FontStyle / TextLineStyle types used
// by the HtmlUtils overloads below.
#include "core/components_ng/pattern/text/span/span_string.h"
#include "core/text/html_utils.h"

namespace OHOS::Ace::NG {
void VibratorUtils::StartVibraFeedback(const std::string& /*vibratorType*/)
{
}
void VibratorUtils::StartViratorDirectly(const std::string& /*vibratorType*/)
{
}
} // namespace OHOS::Ace::NG

namespace OHOS::Ace {
RefPtr<MutableSpanString> HtmlUtils::FromHtml(const std::string& /*str*/)
{
    return nullptr;
}
std::string HtmlUtils::ToHtml(const SpanString* /*str*/)
{
    return std::string();
}
std::string HtmlUtils::ToHtml(const std::list<RefPtr<NG::SpanItem>>& /*spanItems*/)
{
    return std::string();
}
std::string HtmlUtils::ToHtmlForNormalType(
    const NG::FontStyle& /*fontStyle*/, const NG::TextLineStyle& /*textLineStyle*/, const std::u16string& /*content*/)
{
    return std::string();
}
} // namespace OHOS::Ace

// ---------------------------------------------------------------------------
// MultiTypeRecordImpl (concrete clipboard record). Real impl is iOS/OHOS-only;
// inert versions suffice for the M1 shell (no clipboard on the render path).
// ---------------------------------------------------------------------------
#include "adapter/linux/osal/multiType_record_impl.h"

namespace OHOS::Ace {
void MultiTypeRecordImpl::SetPlainText(const std::string /*plainText*/) {}
void MultiTypeRecordImpl::SetUri(const std::string /*uri*/) {}
void MultiTypeRecordImpl::SetPixelMap(RefPtr<PixelMap> /*pixelMap*/) {}
void MultiTypeRecordImpl::SetHtmlText(const std::string& /*htmlText*/) {}
std::vector<uint8_t>& MultiTypeRecordImpl::GetSpanStringBuffer()
{
    static std::vector<uint8_t> buffer;
    return buffer;
}
} // namespace OHOS::Ace

// ---------------------------------------------------------------------------
// Singleton services with no linux implementation. Pointer-returning factories
// return nullptr (callers on non-startup paths null-check); reference-returning
// abstract singletons get a minimal inert concrete subclass.
// ---------------------------------------------------------------------------
#include "base/network/download_manager.h"
#include "base/window/foldable_window.h"
#include "core/common/setting_data_manager.h"
#include "core/common/udmf/udmf_client.h"
#include "core/common/xcollie/xcollieInterface.h"

namespace OHOS::Ace {

DownloadManager* DownloadManager::GetInstance()
{
    return nullptr;
}

RefPtr<FoldableWindow> FoldableWindow::CreateFoldableWindow(int32_t /*instanceId*/)
{
    return nullptr;
}

UdmfClient* UdmfClient::GetInstance()
{
    return nullptr;
}

// XcollieInterface has only non-pure virtuals (default empty bodies) -> a bare
// instance is concrete; return a static one.
XcollieInterface& XcollieInterface::GetInstance()
{
    static XcollieInterface instance;
    return instance;
}

namespace {
class LinuxStubSettingDataManager final : public SettingDataManager {
public:
    int32_t Initialize() override { return 0; }
    bool IsInitialized() const override { return false; }
    int32_t GetCurrentUserId() override { return INVALID_USER_ID; }
    int32_t RegisterObserver(const std::string&, const DataUpdateFunc&, int32_t) override { return 0; }
    int32_t UnregisterObserver(const std::string&, int32_t) override { return 0; }
    int32_t GetStringValue(const std::string&, std::string&, int32_t) const override { return 0; }
    int32_t GetInt32ValueStrictly(const std::string&, int32_t&, int32_t) const override { return 0; }
};
} // namespace

SettingDataManager& SettingDataManager::GetInstance()
{
    static LinuxStubSettingDataManager instance;
    return instance;
}

} // namespace OHOS::Ace

// ---------------------------------------------------------------------------
// Reporter (abstract, reference-returning) + UiSessionManager (pointer).
// ---------------------------------------------------------------------------
#include "core/common/reporter/reporter.h"
#include "interfaces/inner_api/ui_session/ui_session_manager.h"

namespace OHOS::Ace {
namespace NG {
namespace {
class LinuxStubReporter final : public Reporter {
public:
    void HandleUISessionReporting(const JsonReport&) const override {}
    void HandleInputEventInspectorReporting(const TouchEvent&) const override {}
    void HandleInputEventInspectorReporting(const MouseEvent&) const override {}
    void HandleInputEventInspectorReporting(const AxisEvent&) const override {}
    void HandleInputEventInspectorReporting(const KeyEvent&) const override {}
    void HandleWindowFocusInspectorReporting(bool) const override {}
    void HandleInspectorReporting(const JsonReport&) const override {}
};
} // namespace

Reporter& Reporter::GetInstance()
{
    static LinuxStubReporter instance;
    return instance;
}
} // namespace NG

namespace {
// Minimal concrete UiSessionManager so callers (e.g. StageManager::PushPage ->
// OnRouterChange) get a valid instance instead of dereferencing nullptr. All of the
// base-class hooks are no-op virtuals, so the default subclass is inert.
class LinuxStubUiSessionManager final : public UiSessionManager {};
} // namespace

UiSessionManager* UiSessionManager::GetInstance()
{
    static LinuxStubUiSessionManager instance;
    return &instance;
}
} // namespace OHOS::Ace

// ---------------------------------------------------------------------------
// Picker audio-haptic factory + environment proxy. UIKit/iOS-only real impls;
// inert versions here.
// ---------------------------------------------------------------------------
#include "adapter/ios/entrance/picker/picker_haptic_factory.h"
#include "adapter/ios/capability/environment/environment_proxy_impl.h"

namespace OHOS::Ace {

namespace NG {
std::shared_ptr<IPickerAudioHaptic> PickerAudioHapticFactory::GetInstance(
    const std::string& /*uri*/, const std::string& /*effectId*/)
{
    return nullptr;
}
} // namespace NG

namespace Platform {

EnvironmentProxyImpl* EnvironmentProxyImpl::GetInstance()
{
    return nullptr;
}

RefPtr<Environment> EnvironmentProxyImpl::GetEnvironment(const RefPtr<TaskExecutor>& /*taskExecutor*/) const
{
    return nullptr;
}

} // namespace Platform
} // namespace OHOS::Ace
