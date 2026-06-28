---
name: build-run-verify
description: Build the ace_linux executable, run it under headless weston, and capture a screenshot to verify rendering. The standard verification gate for every Linux/Wayland port stage. Use whenever you changed adapter/linux or a linux-*.patch and need to prove it compiles, links (0 undefined), opens a window, and renders.
---

All commands run from the OpenHarmony repo root `/root/openharmony/arkui-x`.

## 1. Build ace_linux

```bash
source /root/openharmony/.repo-tool/linux-env.sh   # PATH: build-tools/linux-aarch64 + python + node
PY=$OHOS_ROOT/prebuilts/python/linux-aarch64/current/bin/python3
NINJA=$OHOS_ROOT/prebuilts/build-tools/linux-aarch64/bin/ninja

# After a BUILD.gn / .gni change you MUST re-gen (header/.cpp-only changes don't need it):
rm -f out/hb_args/buildargs.json
$PY build/hb/main.py build --product-name arkui-x --target-os linux --target-cpu arm64 \
  --build-target ace_linux_x 2>&1 | grep -iE "Done\. Made|ERROR at //"   # fake target = full gn gen

# Compile + link (real exe target = adapter/linux/build:ace_linux):
$NINJA -k 0 -w dupbuild=warn -C out/arkui-x ace_linux > /root/openharmony/.repo-tool/ninja.log 2>&1
grep -c "FAILED:" /root/openharmony/.repo-tool/ninja.log        # want 0
ls -la out/arkui-x/arkui/ace_engine/ace_linux                   # the 100+ MB aarch64 ELF
```

Gotchas:
- The link is slow (60–85 MB unstripped .so, lld `--icf=all` + nm/strip post-process — minutes per relink). Run in background for big rebuilds.
- `ninja` exit code via the wrapper can be 0 while it actually failed — trust the `FAILED:` count.
- `pgrep -f "bin/ninja"` matches your own shell command (false positive); use `ps -eo comm | awk '$1=="ninja"'`.
- lld default error-limit is 20 — for the full undefined list, extract the ace_linux link command from ninja.log
  and re-run it from `out/arkui-x` with `-Wl,--error-limit=0` appended.

## 2. Run under headless weston + screenshot

`weston-screenshooter` asserts `width>0` on the headless backend, so screenshots use the client-side
`ACE_SHOT_PPM` env hook in `WindowView::Present` (glReadPixels → binary PPM, Y-flipped). Software GL via mesa llvmpipe.

```bash
export XDG_RUNTIME_DIR=/run/user/0
# weston leaves a stale socket file after SIGKILL — always rm before (re)starting:
pkill -9 -x weston 2>/dev/null; rm -f $XDG_RUNTIME_DIR/wayland-ace*
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
setsid weston --backend=headless-backend.so --width=1024 --height=768 \
  --socket=wayland-ace --idle-time=0 --debug >/tmp/weston.log 2>&1 < /dev/null & disown
sleep 5   # wait for the wayland-ace socket

# LD_LIBRARY_PATH must cover every out/ dir holding a .so (libhilog/libace/libskia_canvaskit/libark_jsruntime…):
LIBPATH=$(find out/arkui-x -name '*.so' | xargs -n1 dirname | sort -u | tr '\n' ':')
# ACE_STAGE_LAUNCH=1 loads the .abc bundle (Stage D); omit it for the bare-window / ACE_TEST_FRAME path.
setsid env XDG_RUNTIME_DIR=/run/user/0 WAYLAND_DISPLAY=wayland-ace LIBGL_ALWAYS_SOFTWARE=1 \
  GALLIUM_DRIVER=llvmpipe ACE_STAGE_LAUNCH=1 LD_LIBRARY_PATH="$LIBPATH" \
  ACE_SHOT_PPM=/root/openharmony/.repo-tool/shots/shot.ppm \
  out/arkui-x/arkui/ace_engine/ace_linux >/tmp/ace.log 2>&1 < /dev/null & disown
sleep 25
```

Verify in `/tmp/ace.log`:
- `WaylandContext::Connect: ok` → `EnsureEgl: ok` → `surface ... ready` → `WindowView::Present: screenshot written`
- `grep -c OnRsFrame /tmp/ace.log` > 0 means RS produced real frames (a loaded .ets page); 0 = only the
  entrance clear color / no page yet.
- No `RunScriptBuffer failed` / `double free` / `Failed to open the file`.

## 3. PPM → PNG (no PIL/convert in this env — hand-roll with python zlib)

```python
import struct, zlib
with open('shot.ppm','rb') as f:
    assert f.readline().strip()==b'P6'; w,h=map(int,f.readline().split()); f.readline(); data=f.read()
def png(w,h,rgb):
    raw=bytearray()
    for y in range(h): raw.append(0); raw+=rgb[y*w*3:(y+1)*w*3]
    ch=lambda t,d: struct.pack('>I',len(d))+t+d+struct.pack('>I',zlib.crc32(t+d)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+ch(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+ch(b'IDAT',zlib.compress(bytes(raw),9))+ch(b'IEND',b'')
open('shot.png','wb').write(png(w,h,data))
# sample pixels to confirm content vs the entrance clear color RGB(31,31,36)
```

Then `Read` the PNG to view it. Entrance clear color = `glClearColor(0.12,0.12,0.14)` ≈ RGB(31,31,36);
`ACE_TEST_FRAME=1` injects a cyan/magenta/yellow 3-band test frame through the real `OnRsFrame` path.
