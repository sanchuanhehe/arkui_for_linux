---
name: format-changes
description: Run clang-format on the current git changes in arkui_for_linux (adapter/linux). Only reformat changed lines, not whole files. Use after editing C++ in entrance/osal/stage.
---

## Tool Locations (aarch64 OHOS prebuilt clang)

- **clang-format**: `prebuilts/clang/ohos/linux-aarch64/llvm/bin/clang-format`
- **clang-format-diff.py**: `prebuilts/clang/ohos/linux-aarch64/llvm/share/clang/clang-format-diff.py`

(Paths are relative to the OpenHarmony repo root `/root/openharmony/arkui-x`, NOT this sub-repo.)

## Approach

`clang-format-diff.py` reads a diff from stdin and only reformats lines that were changed — it never touches unchanged lines, so it won't reflow thousands of pre-existing lines.

### Working command (run from the OpenHarmony repo root)

```bash
cd /root/openharmony/arkui-x
git -C foundation/arkui/ace_engine/adapter/linux diff HEAD -- '*.cpp' '*.h' '*.cc' 2>/dev/null | \
  python3 prebuilts/clang/ohos/linux-aarch64/llvm/share/clang/clang-format-diff.py \
  -p1 -binary prebuilts/clang/ohos/linux-aarch64/llvm/bin/clang-format -i
```

Notes:
- `git -C foundation/arkui/ace_engine/adapter/linux diff HEAD` — the sub-repo's own changes (this is the
  `arkui_for_linux` repo). Paths in its diff are relative to the sub-repo, so `-p1` strips the leading path
  component and `-i` must be applied from a cwd where the resulting paths resolve — run the
  clang-format-diff with cwd = the sub-repo if `-p1` doesn't line up, e.g.:
  ```bash
  cd foundation/arkui/ace_engine/adapter/linux
  git diff HEAD -- '*.cpp' '*.h' '*.cc' | \
    python3 ../../../../../prebuilts/clang/ohos/linux-aarch64/llvm/share/clang/clang-format-diff.py \
    -p1 -binary ../../../../../prebuilts/clang/ohos/linux-aarch64/llvm/bin/clang-format -i
  ```
- Only reformats changed lines; if they already match the style, no edits are made.
- Run after editing any `.cpp/.h/.cc` under `entrance/`, `osal/`, `stage/` before committing.
