# Pinned third-party versions

`third_party/nanovg` is a git submodule of this repository (see `.gitmodules`),
pinned at the commit below. `git clone --recurse-submodules` fetches it. Tracy
is optional and not vendored yet; add it as a submodule and delete its line
from `.gitignore` when `PSYCHNANOVG_TRACY` is wired.

| Dependency | Source | Commit | Date | Notes |
|---|---|---|---|---|
| NanoVG | https://github.com/memononen/nanovg | `ce3bf745eb2d2dbc14a50bf2446783f691ac4353` | 2026-02-19 | Submodule at `third_party/nanovg`. `build.m`, `README.md`, and the CI workflow fall back to a plain clone at this commit when the submodule is not initialized. |
| glad 2 | https://github.com/Dav1dde/glad, `glad2` PyPI package 2.0.8 | generated output, committed | 2026-09-22 | `gl:compatibility=3.3`, no extensions, no internal loader. Regenerate with `uv run glad --api="gl:compatibility=3.3" --extensions="" --out-path=../third_party/glad --reproducible c` from `gen/`. |
| Tracy | https://github.com/wolfpld/tracy | not cloned | | Optional. `PSYCHNANOVG_TRACY` stays OFF in phase 1. See the deviations section of `SPEC.md`. |

## How to fetch

Run this from the root of this repository:

    git clone https://github.com/memononen/nanovg.git third_party/nanovg
    git -C third_party/nanovg checkout ce3bf745eb2d2dbc14a50bf2446783f691ac4353

`.github/workflows/ci.yml` does the same on a runner, and reads the commit out
of this file, so keep the hash in the table above in its full 40 character
form.

`third_party/glad` is committed. You do not need to generate it.
