/*
 * Copyright (c) 2024-2025 Huawei Device Co., Ltd.
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

#include "core/common/container.h"
#include "core/common/ime/input_method_manager.h"
#include "core/common/ime/text_input_connection.h"
#include "core/components_ng/pattern/text_field/text_field_manager.h"
#include "core/pipeline_ng/pipeline_context.h"

namespace OHOS::Ace {
std::unique_ptr<InputMethodManager> InputMethodManager::instance_ = nullptr;
std::mutex InputMethodManager::mtx_;

InputMethodManager* InputMethodManager::GetInstance()
{
    if (instance_ == nullptr) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (instance_ == nullptr) {
            instance_.reset(new InputMethodManager);
        }
    }
    return instance_.get();
}

void InputMethodManager::OnFocusNodeChange(const RefPtr<NG::FrameNode>& curFocusNode, FocusReason focusReason)
{
    ManageFocusNode(curFocusNode, focusReason);
}

void InputMethodManager::ManageFocusNode(
    const RefPtr<NG::FrameNode>& curFocusNode, FocusReason focusReason, bool saveKeyboard)
{
    if (!curFocusNode) {
        TAG_LOGE(AceLogTag::ACE_KEYBOARD, "CurFocusNode Not Exist");
        return;
    }
    curFocusNode_ = curFocusNode;
    auto pattern = curFocusNode->GetPattern();
    if (pattern) {
        pattern->OnFocusNodeChange(focusReason);
    }
    if (saveKeyboard) {
        TAG_LOGI(AceLogTag::ACE_KEYBOARD, "No Need To Close Keyboard");
        return;
    }
    CloseKeyboard(curFocusNode);
}

void InputMethodManager::ProcessKeyboardInWindowScene(const RefPtr<NG::FrameNode>& curFocusNode) {}

void InputMethodManager::ProcessKeyboard(const RefPtr<NG::FrameNode>& curFocusNode) {}

void InputMethodManager::SetWindowFocus(bool windowFocus) {}

bool InputMethodManager::NeedSoftKeyboard() const
{
    return false;
}

void InputMethodManager::CloseKeyboard(bool disableNeedToRequestKeyboard)
{
    // [linux-port] desktop IME stub — the (bool) overload (called by CloseKeyboard(
    // FrameNode) below) was dropped when this file was ported; restore it as a no-op.
}

void InputMethodManager::CloseKeyboard(int32_t instanceId)
{
    // [linux-port] desktop IME stub.
}

void InputMethodManager::CloseKeyboard(const RefPtr<NG::FrameNode>& focusNode)
{
    auto curPattern = focusNode->GetPattern<NG::Pattern>();
    CHECK_NULL_VOID(curPattern);
    ACE_LAYOUT_SCOPED_TRACE("CloseKeyboard[id:%d]", focusNode->GetId());
    bool isNeedKeyBoard = curPattern->NeedSoftKeyboard();
    if (!isNeedKeyBoard) {
        TAG_LOGI(AceLogTag::ACE_KEYBOARD, "FrameNode(%{public}s/%{public}d) notNeedSoftKeyboard.",
            focusNode->GetTag().c_str(), focusNode->GetId());
        CloseKeyboard();
    }
}

void InputMethodManager::CloseKeyboardInPipelineDestroy() {}

void InputMethodManager::HideKeyboardAcrossProcesses() {}

void InputMethodManager::ProcessModalPageScene() {}

void InputMethodManager::ShowKeyboard(bool isFocusViewChanged, int32_t instanceId)
{
    // [linux-port] desktop IME stub: soft keyboard not used on the Wayland client.
}

void InputMethodManager::SetEditingState(const TextEditingValue& value, int32_t instanceId, bool needFireChangeEvent)
{
    // [linux-port] desktop IME stub.
}

void InputMethodManager::Attach(const WeakPtr<TextInputClient>& client, const TextInputConfiguration& config,
    const RefPtr<TaskExecutor>& taskExecutor, int32_t instanceId)
{
    // [linux-port] desktop IME stub.
}

void InputMethodManager::FinishComposing(int32_t instanceId)
{
    // [linux-port] desktop IME stub.
}
} // namespace OHOS::Ace