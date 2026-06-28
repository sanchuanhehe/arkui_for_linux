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

// Linux/Wayland native port (M1 link): UiTranslateManagerImpl::{AddPixelMap,
// GetAllPixelMap} are referenced by force-linked core (pipeline_context.cpp /
// image_pattern.cpp) but linux has no ace_translate_manager.cpp of its own (mirrors
// adapter/preview/entrance/ace_translate_manager.cpp). Translate/pixelmap-capture is
// an accessibility/translation feature, off the .ets render path, so inert is fine.

#include "core/common/ace_translate_manager.h"

#include <cstdint>

#include "base/utils/macros.h"

namespace OHOS::Ace {
void UiTranslateManagerImpl::AddPixelMap(int32_t nodeId, RefPtr<PixelMap> pixelMap) {}
void UiTranslateManagerImpl::GetAllPixelMap(RefPtr<NG::FrameNode> pageNode) {}
} // namespace OHOS::Ace
