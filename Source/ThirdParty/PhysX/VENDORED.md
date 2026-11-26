# Bundled NVIDIA PhysX SDK

- Version: 4.1 (github.com/NVIDIAGameWorks/PhysX, branch `4.1`, commit a2c0428acab643e60618c681b501e86f7fd558cc)
- Toolset: MSVC v143 (14.44), x64, dynamic CRT (/MDd for debug, /MD for profile)
- License: original LICENSE.md retained in `4.1/`

## Layout

- `4.1/include`: `physx/include` and `pxshared/include` merged into one folder
- `4.1/lib/x64/debug`: PhysX `debug` build. Used by the engine's Debug configuration
- `4.1/lib/x64/profile`: PhysX `profile` build (optimized, PVD enabled). Used by Release and Shipping

Each lib folder holds the core DLLs and their import libraries
(PhysX, PhysXCommon, PhysXFoundation, PhysXCooking) and the static
Extensions, PvdSDK, Vehicle and CharacterKinematic libraries.
GPU (PhysXGpu/PhysXDevice), samples, snippets, PDBs and the `checked`/`release` builds are excluded.

## Rebuilding

CMake settings match the `vc17win64` preset below, which is not part of the upstream repo.
Python is not needed; run CMake directly from `physx/compiler/vc17win64`.

```
PX_BUILDSNIPPETS=False  PX_BUILDPUBLICSAMPLES=False  PX_GENERATE_STATIC_LIBRARIES=False
NV_USE_STATIC_WINCRT=False  NV_USE_DEBUG_WINCRT=True  PX_FLOAT_POINT_PRECISE_MATH=False
```

```
set PHYSX_ROOT_DIR=<physx>
set PM_CMakeModules_PATH=<physx>/../externals/cmakemodules
cmake <physx>/compiler/public -G "Visual Studio 18 2026" -A x64 -T v143 ^
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DTARGET_BUILD_PLATFORM=windows -DPX_OUTPUT_ARCH=x86 ^
  -DPHYSX_ROOT_DIR=<physx> -DPX_OUTPUT_LIB_DIR=<physx> -DPX_OUTPUT_BIN_DIR=<physx> ^
  -DCMAKE_PREFIX_PATH="<physx>/../externals/cmakemodules;<physx>/../externals/targa" ^
  -DPX_BUILDSNIPPETS=FALSE -DPX_BUILDPUBLICSAMPLES=FALSE -DPX_GENERATE_STATIC_LIBRARIES=FALSE ^
  -DNV_USE_STATIC_WINCRT=FALSE -DNV_USE_DEBUG_WINCRT=TRUE -DPX_FLOAT_POINT_PRECISE_MATH=FALSE
msbuild PhysXSDK.slnx -p:Configuration=debug   -p:Platform=x64
msbuild PhysXSDK.slnx -p:Configuration=profile -p:Platform=x64
```

Output goes to `physx/bin/win.x86_64.vc142.md/<config>` (the folder name stays vc142 even with the v143 toolset).
CMake 4.x needs `CMAKE_POLICY_VERSION_MINIMUM=3.5` because the upstream scripts declare an older minimum version.

No external SDK installation or environment variable is required to build the engine.
