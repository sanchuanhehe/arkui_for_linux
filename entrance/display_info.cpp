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

// Linux/Wayland native port: ported from adapter/macos/entrance/display_info.mm.
// macOS read NSScreen (backingScaleFactor/frame) + an iOS device->ppi table.
// On Linux the real output geometry/scale comes from wl_output (set later via
// SetWidth/SetHeight + the wl_output scale in the windowing stage); the
// constructor uses sane desktop defaults (scale 1.0, 96 dpi) so density math is
// valid before the first wl_output event arrives.

#include "display_info.h"

#include <new>
#include <parcel.h>

namespace OHOS::Rosen {

namespace {
constexpr int32_t LINUX_DEFAULT_DPI = 96; // standard desktop logical dpi
}

DisplayInfo::DisplayInfo()
{
    id_ = 0;
    // Desktop displays are landscape; Wayland has no device orientation.
    displayOrientation_ = DisplayOrientation::LANDSCAPE;

    // Real geometry/scale arrive from wl_output later; default to 1.0 scale.
    const float scale = 1.0f;
    width_ = 0;
    height_ = 0;
    densityPixels_ = scale;
    scaledDensity_ = scale;
    densityDpi_ = GetDevicePpi();
}

DisplayInfo::~DisplayInfo()
{
}

DisplayId DisplayInfo::GetDisplayId() const
{
    return id_;
}
int32_t DisplayInfo::GetWidth() const
{
    return width_;
}
int32_t DisplayInfo::GetHeight() const
{
    return height_;
}
Orientation DisplayInfo::GetOrientation() const
{
    return orientation_;
}
DisplayOrientation DisplayInfo::GetDisplayOrientation() const
{
    return displayOrientation_;
}

float DisplayInfo::GetDensityPixels() const
{
    return densityPixels_;
}

float DisplayInfo::GetScaledDensity() const
{
    return scaledDensity_;
}

int32_t DisplayInfo::GetDensityDpi() const
{
    return densityDpi_;
}

void DisplayInfo::SetDisplayId(DisplayId displayId)
{
    id_ = displayId;
}
void DisplayInfo::SetWidth(int32_t width)
{
    width_ = width;
}
void DisplayInfo::SetHeight(int32_t height)
{
    height_ = height;
}
void DisplayInfo::SetOrientation(Orientation orientation)
{
    orientation_ = orientation;
}
void DisplayInfo::SetDisplayOrientation(DisplayOrientation displayOrientation)
{
    displayOrientation_ = displayOrientation;
}

void DisplayInfo::SetDensityPixels(float densityPixels)
{
    densityPixels_ = densityPixels;
}

void DisplayInfo::SetScaledDensity(float scaledDensity)
{
    scaledDensity_ = scaledDensity;
}

void DisplayInfo::SetDensityDpi(int32_t dpi)
{
    densityDpi_ = dpi;
}

int32_t DisplayInfo::GetDevicePpi() const
{
    // Wayland exposes physical mm + mode px via wl_output (geometry/mode); a
    // precise ppi can be derived there in the windowing stage. Until then use
    // the standard desktop logical dpi so downstream density math is stable.
    return LINUX_DEFAULT_DPI;
}

int32_t DisplayInfo::GetXDpi()
{
    return 0;
}

int32_t DisplayInfo::GetYDpi()
{
    return 0;
}

} // namespace OHOS::Rosen
