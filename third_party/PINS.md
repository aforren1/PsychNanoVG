# Pinned third-party versions

`third_party/nanovg` is a git submodule of this repository (see `.gitmodules`),
pinned at the commit below. `git clone --recurse-submodules` fetches it. Tracy
is optional and not vendored: a developer who wants to profile clones it by
hand, and `.gitignore` keeps the clone out of the repository.

| Dependency | Source | Commit | Date | Notes |
|---|---|---|---|---|
| NanoVG | https://github.com/memononen/nanovg | `ce3bf745eb2d2dbc14a50bf2446783f691ac4353` | 2026-02-19 | Submodule at `third_party/nanovg`. `build.m`, `README.md`, and the CI workflow fall back to a plain clone at this commit when the submodule is not initialized. |
| glad 2 | https://github.com/Dav1dde/glad, `glad2` PyPI package 2.0.8 | generated output, committed | 2026-09-22 | `gl:compatibility=3.3`, no extensions, no internal loader. Regenerate with `uv run glad --api="gl:compatibility=3.3" --extensions="" --out-path=../third_party/glad --reproducible c` from `gen/`. Phase 3 added `gles2=3.0` beside it for the GLES backends (`include/glad/gles2.h`, `src/gles2.c`, 2026-09-23): `uv run glad --api="gles2=3.0" --extensions="" --out-path=../third_party/glad --reproducible c`. |
| Tracy | https://github.com/wolfpld/tracy | tag `v0.11.1` (`5d542dc`; the CI reads the first 40 character hash in this file as the NanoVG pin, so this one stays short), not vendored | 2024-08-22 | Optional. Clone it into `third_party/tracy` and build with `PSYCHNANOVG_TRACY=1` (see `README.md`). v0.11.1 is the version that was built and captured on 2026-09-22. The client and the server must be the same version. |

## How to fetch

Run this from the root of this repository:

    git clone https://github.com/memononen/nanovg.git third_party/nanovg
    git -C third_party/nanovg checkout ce3bf745eb2d2dbc14a50bf2446783f691ac4353

`.github/workflows/ci.yml` does the same on a runner, and reads the commit out
of this file, so keep the hash in the table above in its full 40 character
form.

`third_party/glad` is committed. You do not need to generate it.

To profile with Tracy:

    git clone --branch v0.11.1 https://github.com/wolfpld/tracy.git third_party/tracy
