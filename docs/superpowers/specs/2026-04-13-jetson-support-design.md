# Jetson Support Design

Date: 2026-04-13
Project: `proj2_20260411`
Scope: add first-stage Jetson AGX Orin / JetPack 5.1.2 support while preserving the current Windows build and keeping the external detection interface as stable as practical.

## Goal

Enable the project to:

- continue building and running on Windows
- build and run natively on Jetson AGX Orin with JetPack 5.1.2
- preserve a library-style interface similar to the current exported entrypoint
- prioritize getting Jetson running first, rather than performing a broad architecture cleanup

The success criterion for the first Jetson stage is:

- build a Linux shared library on Jetson
- load configuration and models
- run inference on at least one real image
- preserve the current JSON-oriented input/output contract as closely as practical

## Non-Goals

This first Jetson support stage does not attempt to:

- fully refactor the codebase into a clean platform abstraction layer
- eliminate all Windows-specific code from shared files
- unify all platform differences behind a new core library architecture
- preserve every implementation detail of the current Windows shell
- provide cross-compilation from Windows to Jetson
- add macOS support

## Target Environment

Jetson target:

- device: AGX Orin
- system: JetPack 5.1.2
- execution model: native build and native inference on the device

Windows target:

- remain supported as the existing working environment

## Design Decision

Use a Jetson-first, minimum-intrusion approach for the first stage.

Specifically:

- keep Windows and Jetson coexisting in the same codebase
- use `#ifdef _WIN32` / `#ifdef __linux__` in a limited set of files to get Jetson working quickly
- avoid a broad “extract platform-neutral core” refactor in stage one
- preserve the external detection entrypoint shape as much as possible
- introduce a Linux-specific test harness instead of forcing the existing Windows shell to serve both platforms

This is chosen because it minimizes time-to-first-success on Jetson and avoids spending the first stage on broad architectural cleanup.

## External Interface Strategy

The current exported interface is:

```cpp
extern "C" __declspec(dllexport) char* detect_process(char* file_Data, int* det_state, int* iPID = nullptr);
```

The first-stage Jetson design keeps the conceptual interface intact:

- same exported function name: `detect_process`
- same JSON-in / JSON-out model
- same integer detection status contract

Platform-specific differences are limited to symbol export mechanics:

- Windows: DLL export
- Linux/Jetson: shared-object symbol visibility

This keeps upper-layer integration changes small.

## Runtime Shape

### Windows

Preserve the current pattern:

- `proj2.dll`
- Windows test shell
- `config/` adjacent runtime layout

### Jetson

Add a native Linux runtime shape:

- `libproj2.so`
- a Linux-specific lightweight test harness for validation
- `config/` adjacent runtime layout in the first stage

The configuration layout should remain conceptually compatible with Windows so that the inference workflow is not redesigned during stage one.

## Implementation Approach

### Platform strategy

First-stage support uses selective in-file branching rather than a full abstraction rewrite.

Use `#ifdef` branching only where platform-specific APIs currently block Linux:

- symbol export macros
- path discovery
- UUID generation
- directory/file utility calls
- logging/string conversion utilities
- dynamic loading in test harness code

### Test harness strategy

Do not force the Windows test shell to serve Linux.

Instead:

- keep the current Windows shell path intact
- add a Linux/Jetson-specific test harness for native device validation

This keeps the fastest path to Jetson functionality while preserving the library interface.

## File Impact Strategy

### High-priority files for stage one

These files are the first expected Jetson blockers and should be addressed before touching deeper inference logic:

- `public/DetAlgorithm.h`
- `proj2/detect.cpp`
- `proj2/mylog.h`
- `proj2/mylog.cpp`
- `shell/shell.cpp`

### Lower-priority files

These should remain as untouched as possible in stage one unless Linux compilation proves otherwise:

- `proj2/area.cpp`
- `proj2/koujian.cpp`
- `proj2/element.cpp`
- `proj2/tensorrt.cpp`
- `proj2/yolov5Trt.cpp`

The principle is:

- fix entry-layer and platform-utility blockers first
- avoid deep algorithm changes unless Jetson compilation or runtime forces them

## Third-Party Dependency Migration

### Must be replaced with Jetson-native environment

#### TensorRT

TensorRT is a primary migration concern.

Reasons:

- engine files are often platform- and version-bound
- Windows-built or version-mismatched engines cannot be assumed to work on Jetson
- Jetson should use the local TensorRT environment provided by JetPack

First-stage rule:

- Jetson builds and runtime must use Jetson-native TensorRT headers and libraries
- engine files may need regeneration on the device or against a Jetson-compatible TensorRT stack

#### CUDA

CUDA is also platform-native on Jetson.

First-stage rule:

- Jetson builds and runtime must use Jetson-native CUDA
- do not reuse the Windows vendored CUDA directory for Linux builds

#### ATL / MFC

These are Windows-only and must be replaced on Linux.

This mostly affects the logging and string/time utility layer.

#### Windows RPC / UUID

The current UUID path is Windows-specific and must be replaced on Linux.

### Must change integration style, but core usage likely remains

#### OpenCV

OpenCV image-processing code is likely reusable, but:

- Windows-style `opencv_world*.lib/.dll` linkage does not map directly to Jetson/Linux
- Jetson should use native OpenCV packages or native library discovery

### Likely reusable as-is or with minimal change

#### nlohmann/json

- header-only
- low migration risk

#### pugixml

- source-based integration
- low migration risk

#### Boost

- likely low risk for the currently used portions
- not a first-stage migration focus

## Platform-Specific API Replacement Plan

### 1. Export macro replacement

Introduce a platform-aware export macro in the public interface header.

Objective:

- preserve exported symbol semantics on Windows
- expose the same symbol on Linux shared libraries

### 2. Windows-only dynamic loading in the test shell

Keep the Windows shell intact and add a Linux-native validation shell.

Objective:

- avoid mixing `LoadLibrary`/`GetProcAddress` and Linux `dlopen` logic in the same first-stage file
- keep the Windows validation flow stable

### 3. File and directory APIs

Replace or wrap uses of:

- `_access`
- `_mkdir`
- `io.h`
- `direct.h`

Prefer:

- `std::filesystem`
- Linux-compatible path checks and directory creation

### 4. Module/executable path discovery

Current Windows logic depends on `GetModuleFileName`.

Linux replacement should use a Linux-native executable path strategy such as `/proc/self/exe`.

### 5. UUID generation

Current Windows UUID logic depends on `rpc.h`.

Linux replacement should use a Linux-friendly UUID mechanism while preserving output semantics closely enough for current JSON consumers.

### 6. Logging subsystem replacement on Linux

The current logging layer is tightly tied to ATL/MFC.

First-stage design:

- keep the current Windows implementation
- add a Linux branch in the same files with a minimal substitute implementation
- preserve function-level logging API shape so the rest of the code changes as little as possible

This is the highest-risk code area in the first stage, but still preferable to a broad refactor when speed matters.

## Build-System Design

The existing CMake build currently targets Windows.

The first Jetson stage extends it into a dual-platform build:

- `WIN32`
  - build the current Windows outputs
- `UNIX AND NOT APPLE`
  - build a Linux shared library
  - build a Linux-native test harness

### Windows branch

Preserve:

- existing Windows DLL target
- existing Windows-oriented dependency handling
- current runnable release staging behavior

### Linux/Jetson branch

Add:

- Linux shared library target
- Linux test harness target
- Jetson-native dependency discovery for TensorRT, CUDA, and OpenCV

The first stage does not require a perfect cross-platform CMake design. It only needs a reliable native Windows branch and a reliable native Jetson branch.

## Recommended Execution Order

The fastest path to Jetson success is:

1. Add platform-aware export macro
2. Add Linux/Jetson test harness
3. Replace or branch file/directory/platform path utilities
4. Replace or branch UUID generation
5. Add Linux branch for the logging subsystem
6. Extend CMake for Linux/Jetson builds
7. Compile on Jetson
8. Run a real image on Jetson
9. Regenerate or validate engine files if TensorRT compatibility fails

This sequence is preferred over:

- rewriting the entire architecture first
- making CMake perfect before removing Windows API blockers
- starting with deep algorithm files

## Risks

### 1. Engine incompatibility

Even after Linux compilation succeeds, inference may fail if the engine files are not compatible with Jetson’s TensorRT stack.

### 2. Logging replacement complexity

The ATL/MFC logging layer is one of the strongest Windows ties and may require more code than initially expected.

### 3. Hidden Windows assumptions outside the first target set

Even after the planned file set is updated, deeper files may still contain Windows assumptions uncovered only during Jetson compilation.

### 4. Runtime path assumptions

The current config lookup behavior assumes a Windows-style adjacent runtime layout. Linux may expose additional path-resolution issues during first execution.

## Acceptance Criteria For Stage One

The first Jetson-support stage is accepted when:

1. Windows still builds and runs with the current interface
2. Jetson builds a Linux shared library natively
3. A Linux-native validation shell can call `detect_process`
4. Configuration loads on Jetson
5. At least one real image runs through the inference path on Jetson
6. The JSON-based protocol remains compatible enough for upstream consumers

## Why This Design

This design is intentionally biased toward speed of delivery rather than architectural purity.

It chooses:

- selective `#ifdef` branching
- a Linux-native shell
- minimal public-interface disruption

because the current priority is “get Jetson running soon” while preserving Windows support, not “complete platform architecture cleanup in one pass.”

