# Bundled NVIDIA NvCloth SDK

- Version: 1.1.6 (github.com/NVIDIAGameWorks/NvCloth, tag `1.1.6`)
- Toolset: Visual Studio 2026 (`v145`, MSVC 19.52), x64, dynamic CRT (`/MDd` for debug, `/MD` for release)
- Backends: CPU (SSE2 / AVX) only (`NV_CLOTH_ENABLE_CUDA=0`, `NV_CLOTH_ENABLE_DX11=0`)
- License: original `license.txt` retained in `1.1.6/`

## Layout

- `1.1.6/include`: `NvCloth/include` (`NvCloth/*`) and `NvCloth/extensions/include` (`NvClothExt/*`) merged into one folder. `foundation/*` headers are shared from `Source/ThirdParty/PhysX/4.1/include`.
- `1.1.6/lib/x64/debug`: NvCloth `debug` build (`NvClothDEBUG_x64.dll`, `NvClothDEBUG_x64.lib`). Used by the engine's Debug configuration.
- `1.1.6/lib/x64/release`: NvCloth `release` build (`NvCloth_x64.dll`, `NvCloth_x64.lib`). Used by Release and Shipping.

Samples, CUDA/DX11 backends, PDBs, MAPs, and `checked`/`profile` builds are excluded.

## Patches Applied for VS 2026

1. `NvCloth/include/NvCloth/ps/PsAllocator.h`: `#include <typeinfo.h>` → `#include <typeinfo>` (deprecated header removed in modern MSVC).
2. `NvCloth/compiler/cmake/windows/CMakeLists.txt`: removed `/WX` and `/Wall` (kept `/W4`) for modern MSVC warning compatibility.
3. `NvCloth/compiler/cmake/windows/NvCloth.cmake`: removed `/DELAYLOAD:nvcuda.dll` since CUDA is disabled.
4. `NvCloth/include/NvCloth/Callbacks.h`: guarded `physx::PxAssertHandler` with `#ifndef PX_FOUNDATION_PX_ASSERT_H` to avoid redefinition when included alongside PhysX 4.1's `foundation/PxAssert.h`.

## Rebuilding

```powershell
$env:GW_DEPS_ROOT = "<path-to-cloned-NvCloth-repo>"
cmake -S "$env:GW_DEPS_ROOT\NvCloth\compiler\cmake\windows" -B <short-build-dir> `
  -G "Visual Studio 18 2026" -A x64 `
  "-DCMAKE_POLICY_VERSION_MINIMUM=3.5" `
  "-DTARGET_BUILD_PLATFORM=windows" `
  "-DNV_CLOTH_ENABLE_CUDA=0" `
  "-DNV_CLOTH_ENABLE_DX11=0" `
  "-DPX_OUTPUT_DLL_DIR=<short-build-dir>\bin" `
  "-DPX_OUTPUT_LIB_DIR=<short-build-dir>\lib" `
  "-DPX_OUTPUT_EXE_DIR=<short-build-dir>\bin"
cmake --build <short-build-dir> --config debug --parallel
cmake --build <short-build-dir> --config release --parallel
```

No external SDK installation or environment variable is required to build the engine.
