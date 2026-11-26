# Bundled RecastNavigation SDK

- Version: 1.6.0 (github.com/recastnavigation/recastnavigation, branch `main`, commit `9f4ce64458dfae86e1239c525ddc219c4e9e06f1`)
- Toolset: Visual Studio 2026 (MSVC 19.52), x64, static libraries, dynamic CRT (`/MDd` for debug, `/MD` for release)
- Options: `RECASTNAVIGATION_DT_POLYREF64=OFF`, `RECASTNAVIGATION_DT_VIRTUAL_QUERYFILTER=OFF`, `RECASTNAVIGATION_ENABLE_FAST_MATH=OFF`
- License: original `License.txt` retained in `1.6.0/`

## Layout

- `1.6.0/include/recastnavigation`: headers for `Recast`, `Detour`, `DetourCrowd`, `DetourTileCache`, `DebugUtils`, and generated `version.h`. Both `include` and `include/recastnavigation` are added to `includedirs`.
- `1.6.0/lib/x64/debug`: RecastNavigation `Debug` static libraries (`Recast-d.lib`, `Detour-d.lib`, `DetourCrowd-d.lib`, `DetourTileCache-d.lib`, `DebugUtils-d.lib`). Used by the engine's Debug configuration.
- `1.6.0/lib/x64/release`: RecastNavigation `Release` static libraries (`Recast.lib`, `Detour.lib`, `DetourCrowd.lib`, `DetourTileCache.lib`, `DebugUtils.lib`). Used by Release and Shipping.

`RecastDemo`, `Tests`, examples, PDBs, CMake config files, and pkg-config files are excluded.

## Rebuilding

```powershell
cmake -S <recast-src> -B <build-dir> `
  -G "Visual Studio 18 2026" -A x64 `
  -DBUILD_SHARED_LIBS=OFF `
  -DRECASTNAVIGATION_DEMO=OFF `
  -DRECASTNAVIGATION_TESTS=OFF `
  -DRECASTNAVIGATION_EXAMPLES=OFF `
  "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`$<`$<CONFIG:Debug>:Debug>DLL"
cmake --build <build-dir> --config Debug --parallel
cmake --build <build-dir> --config Release --parallel
cmake --install <build-dir> --config Debug --prefix <stage-debug>
cmake --install <build-dir> --config Release --prefix <stage-release>
```

No external SDK installation or environment variable is required to build the engine.
