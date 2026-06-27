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

// ArkUI-X Linux/Wayland native app entry (Route A).
//
// Stage A: a minimal entry so the ace_linux executable target is gn-resolvable
// and the libace_static_linux build graph gets pulled in. The real Wayland
// window + EGL surface + ability lifecycle bring-up lands in Stage C/D
// (adapter/linux/entrance/wayland_window.* + main loop).

#include <cstdio>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    // Stage C will replace this with: wl_display_connect -> xdg_shell window ->
    // wl_egl_window -> EGLSurface -> ace ability lifecycle -> RS render loop.
    printf("ace_linux: ArkUI-X Linux/Wayland native shell (Route A) — skeleton\n");
    return 0;
}
