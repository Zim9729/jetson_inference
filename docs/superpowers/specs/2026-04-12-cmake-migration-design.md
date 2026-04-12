# CMake Migration Design

Date: 2026-04-12
Project: `proj2_20260411`
Scope: replace the current Visual Studio project files with a CMake-based Windows x64 build that preserves the current runnable layout and behavior.

## Goal

Migrate the project from `.sln` / `.vcxproj`-driven builds to CMake while preserving the current Windows x64 build behavior:

- Build `proj2` as a DLL.
- Build `shell` as a console executable.
- Place both outputs in `release/`.
- Keep `shell.exe` able to load `proj2.dll` from the same directory.
- Keep runtime configuration under `release/config`.
- Resolve third-party dependencies from the local `public/` directory only.

This migration is a build-system replacement, not a source-layout refactor.

## Non-Goals

The first migration pass does not attempt to:

- support non-Windows platforms
- support Win32 as a parity target
- generalize dependency discovery with `find_package`
- add install/export/package flows
- refactor the source tree
- normalize all third-party linkage into reusable package-style targets

## Current State

The repository currently contains:

- `proj2/`: the DLL source tree
- `shell/`: the test harness executable
- `public/`: vendored third-party dependencies
- `config/`: runtime model/config files

Observed Visual Studio behavior:

- `proj2` builds as a `DynamicLibrary` in `Release|x64`
- `shell` builds as an `Application`
- both targets emit to `release/`
- `shell.cpp` dynamically loads `proj2.dll` from the executable directory
- `proj2` depends on OpenCV, CUDA, TensorRT, Boost, pugixml, JSON headers, and `Rpcrt4.lib`
- `shell` depends on OpenCV, Boost, pugixml, JSON headers, and likely only a subset of the originally declared project-level libraries

## Design Decision

Adopt a parity-first CMake migration for Windows x64.

This means:

- preserve the current source directories
- encode the current build graph directly in CMake
- target the currently working local dependency layout under `public/`
- make the generated `release/` directory directly runnable after build

This is preferred over a more abstract or cross-platform design because the current priority is operational replacement, not long-term generalization.

## Target Layout

Add a top-level `CMakeLists.txt` at the repository root.

Optional supporting files may be added under a `cmake/` directory if helper scripts become necessary, for example runtime DLL copy helpers.

The source layout remains:

```text
proj2_20260411/
├─ CMakeLists.txt
├─ proj2/
├─ shell/
├─ public/
├─ config/
└─ release/
```

Expected runtime layout after build:

```text
release/
├─ shell.exe
├─ proj2.dll
├─ config/
└─ runtime DLLs
```

## Build Targets

### `proj2`

Type:

- `SHARED`

Source set:

- `proj2/area.cpp`
- `proj2/DetAlgorithm.cpp`
- `proj2/detect.cpp`
- `proj2/element.cpp`
- `proj2/mylog.cpp`
- `proj2/koujian.cpp`
- `proj2/mycommon.cpp`
- `proj2/proj2.cpp`
- `proj2/tensorrt.cpp`
- `proj2/yolov5Trt.cpp`
- `proj2/proj2.rc`

Public purpose:

- produce `proj2.dll`
- preserve the existing exported C API entrypoint behavior

### `shell`

Type:

- executable

Source set:

- `shell/shell.cpp`

Purpose:

- provide the existing test harness executable
- continue loading `proj2.dll` from the same output directory

## Dependency Mapping

Dependencies are resolved only from the local `public/` tree.

Shared include roots:

- `public/`
- `public/boost_MSVC14.4/include/boost-1_87`
- `public/json-develop/include`
- `public/pugixml1.15`

`proj2` include roots:

- `public/Cuda12.0/include`
- `public/TensorRT-8.6/include`
- `public/OpenCV4.6.0/include`

`shell` include roots:

- `public/OpenCV4.6.0/include`

Library roots:

- `public/boost_MSVC14.4/lib`
- `public/OpenCV4.6.0/lib`
- `public/pugixml1.15/lib`
- `public/Cuda12.0/lib/x64`
- `public/TensorRT-8.6/lib`

### Linkage Strategy

Do not reproduce the Visual Studio `*.lib` wildcard behavior.

Instead:

- explicitly link the minimum required libraries
- keep all linkage local to Windows x64
- add `Rpcrt4.lib` explicitly for UUID-related logic used by `proj2`

Reason:

- wildcard linkage is brittle in CMake
- explicit linkage makes failures diagnosable
- this reduces accidental overlinking

## Compiler and Platform Constraints

The first CMake version should fail fast unless:

- platform is Windows
- pointer size is 8 bytes

Compiler settings should match current intent as closely as practical:

- C++17
- Unicode behavior preserved where required by current source
- `_CRT_SECURE_NO_WARNINGS` and `DC_EXPORTS` applied to `proj2`
- resource compilation enabled for `proj2.rc`

Debug and Win32 do not need full parity in the first pass. The design target is `Release x64`.

## Output Behavior

Both targets should emit runtime artifacts into `release/`.

Required output behavior:

- `shell.exe` in `release/`
- `proj2.dll` in `release/`
- optional import library / pdbs may remain generator-managed, but the primary runtime artifacts must land in `release/`

This keeps the runtime contract unchanged relative to the current manual expectation.

## Post-Build Runtime Layout

After build, CMake should arrange a runnable `release/` directory.

### Required copy steps

1. Copy `config/` to `release/config`
2. Copy required runtime DLLs into `release/`

### Runtime DLL policy

Use a minimal required runtime DLL copy list, not whole-directory replication.

Why:

- avoids unnecessary file bloat
- reduces ambiguity about what is actually required
- makes missing runtime dependencies visible

The actual DLL copy list will be derived from the real contents of `public/` during implementation.

## Compatibility Notes

This migration preserves the current runtime assumptions:

- `shell.cpp` loads `proj2.dll` from the executable directory
- `proj2` expects `config/` under that same runtime root
- model paths in `config/*.xml` remain relative to `config/`

Because of this, the build must continue producing a runnable folder, not just isolated target binaries.

## Known Risks

### 1. Runtime DLL name resolution

The project file declares library directories, but runtime success depends on matching `.dll` files. These DLL names must be verified against the actual files under `public/`.

### 2. Overdeclared Visual Studio dependencies

The `.vcxproj` files may declare libraries that are not actually needed by the compiled code. This is especially likely for `shell`. The CMake migration should prefer source-backed dependency decisions.

### 3. Missing model engine artifacts

The CMake migration can succeed even if runtime models are incomplete. For example, if a referenced `.engine` file is missing but only `.onnx` exists, build success will not imply runtime success.

### 4. Configuration parity vs source issues

Some runtime issues in the current codebase, such as configuration assumptions or missing files, will remain after the CMake migration. This design only replaces the build system.

## Verification Plan

The migration is complete only when the following are true:

1. CMake configures successfully on Windows x64
2. `proj2` builds successfully
3. `shell` builds successfully
4. both artifacts appear in `release/`
5. `config/` is copied to `release/config`
6. runtime DLL copy logic populates `release/`
7. `shell.exe` starts and can at least attempt to load `proj2.dll`

Runtime model inference success is a secondary validation step, because model availability is partly outside the build-system change itself.

## Implementation Sequence

1. Create top-level `CMakeLists.txt`
2. Encode Windows x64 guardrails
3. Define `proj2` target and source list
4. Define `shell` target and source list
5. Add include directories rooted in `public/`
6. Add explicit library directories and explicit linked libraries
7. Set output directories to `release/`
8. Add post-build copy for `config/`
9. Add post-build copy for runtime DLLs
10. Verify configure/build/runtime folder shape

## Acceptance Criteria

The migration is accepted when:

- `cmake` becomes a viable replacement for the current Visual Studio project files for Windows x64
- the produced output directory is directly runnable in the same way as the current manually arranged `release/`
- no source-code refactor is required for the migration itself

## Open Decisions Resolved

- Include `shell` in the migration: yes
- Preserve directly runnable `release/` layout: yes
- Limit first pass to Windows x64 parity: yes
- Keep local vendored dependencies under `public/`: yes

