# CMake Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the current Visual Studio build entrypoint with a Windows x64 CMake build that produces a directly runnable `release/` directory containing `proj2.dll`, `shell.exe`, `config/`, and required runtime DLLs.

**Architecture:** Add a root `CMakeLists.txt` and a small helper script under `cmake/` that define the `proj2` DLL and `shell` EXE, wire them to the vendored dependencies under `public/`, and stage runtime assets into `release/` after build. Preserve the existing source tree and runtime assumptions instead of refactoring code during the migration.

**Tech Stack:** CMake, MSVC/Visual Studio 2022 generator, Windows x64, TensorRT 8.6, CUDA 12.0, OpenCV 4.6.0, Boost headers, nlohmann/json headers, pugixml source build.

---

## File Structure Lock-In

**Create:**

- `E:/0_project/proj2_20260411/CMakeLists.txt`
- `E:/0_project/proj2_20260411/CMakePresets.json`
- `E:/0_project/proj2_20260411/cmake/StageRuntime.cmake`

**Modify:**

- `E:/0_project/proj2_20260411/.gitignore`

**Existing source inputs used by the CMake targets:**

- `E:/0_project/proj2_20260411/proj2/area.cpp`
- `E:/0_project/proj2_20260411/proj2/DetAlgorithm.cpp`
- `E:/0_project/proj2_20260411/proj2/detect.cpp`
- `E:/0_project/proj2_20260411/proj2/element.cpp`
- `E:/0_project/proj2_20260411/proj2/mylog.cpp`
- `E:/0_project/proj2_20260411/proj2/koujian.cpp`
- `E:/0_project/proj2_20260411/proj2/mycommon.cpp`
- `E:/0_project/proj2_20260411/proj2/proj2.cpp`
- `E:/0_project/proj2_20260411/proj2/tensorrt.cpp`
- `E:/0_project/proj2_20260411/proj2/yolov5Trt.cpp`
- `E:/0_project/proj2_20260411/proj2/proj2.rc`
- `E:/0_project/proj2_20260411/public/pugixml1.15/pugixml.cpp`
- `E:/0_project/proj2_20260411/shell/shell.cpp`

**Do not modify in this migration:**

- `E:/0_project/proj2_20260411/proj2.sln`
- `E:/0_project/proj2_20260411/proj2/proj2.vcxproj`
- `E:/0_project/proj2_20260411/shell/shell.vcxproj`

The Visual Studio files remain as a temporary fallback until the CMake build is verified.

### Task 1: Add The Root CMake Entry Point

**Files:**

- Create: `E:/0_project/proj2_20260411/CMakeLists.txt`
- Create: `E:/0_project/proj2_20260411/CMakePresets.json`
- Test: configure in `E:/0_project/proj2_20260411/build/cmake-vs2022-x64`

- [ ] **Step 1: Verify CMake configure currently fails because no root build file exists**

Run:

```powershell
cmake -S E:/0_project/proj2_20260411 -B E:/0_project/proj2_20260411/build/cmake-vs2022-x64 -G "Visual Studio 17 2022" -A x64
```

Expected: FAIL with an error equivalent to "The source directory does not appear to contain CMakeLists.txt".

- [ ] **Step 2: Create the initial root `CMakeLists.txt` with project guardrails, dependency roots, and output layout**

Write this file:

```cmake
cmake_minimum_required(VERSION 3.26)

project(proj2_20260411 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "This project currently supports Windows only.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project currently supports x64 only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(PUBLIC_DIR "${PROJECT_ROOT}/public")
set(RELEASE_DIR "${PROJECT_ROOT}/release")

set(BOOST_INCLUDE_DIR "${PUBLIC_DIR}/boost_MSVC14.4/include/boost-1_87")
set(JSON_INCLUDE_DIR "${PUBLIC_DIR}/json-develop/include")
set(PUGIXML_DIR "${PUBLIC_DIR}/pugixml1.15")
set(OPENCV_INCLUDE_DIR "${PUBLIC_DIR}/OpenCV4.6.0/include")
set(OPENCV_LIB_DIR "${PUBLIC_DIR}/OpenCV4.6.0/lib")
set(OPENCV_BIN_DIR "${PUBLIC_DIR}/OpenCV4.6.0/bin")
set(CUDA_INCLUDE_DIR "${PUBLIC_DIR}/Cuda12.0/include")
set(CUDA_LIB_DIR "${PUBLIC_DIR}/Cuda12.0/lib/x64")
set(TENSORRT_INCLUDE_DIR "${PUBLIC_DIR}/TensorRT-8.6/include")
set(TENSORRT_LIB_DIR "${PUBLIC_DIR}/TensorRT-8.6/lib")

foreach(required_dir
    "${PUBLIC_DIR}"
    "${BOOST_INCLUDE_DIR}"
    "${JSON_INCLUDE_DIR}"
    "${PUGIXML_DIR}"
    "${OPENCV_INCLUDE_DIR}"
    "${OPENCV_LIB_DIR}"
    "${OPENCV_BIN_DIR}"
    "${CUDA_INCLUDE_DIR}"
    "${CUDA_LIB_DIR}"
    "${TENSORRT_INCLUDE_DIR}"
    "${TENSORRT_LIB_DIR}")
    if(NOT EXISTS "${required_dir}")
        message(FATAL_ERROR "Required dependency path is missing: ${required_dir}")
    endif()
endforeach()

file(MAKE_DIRECTORY "${RELEASE_DIR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${RELEASE_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${RELEASE_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${RELEASE_DIR}")

foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${RELEASE_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${RELEASE_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} "${RELEASE_DIR}")
endforeach()
```

- [ ] **Step 3: Add `CMakePresets.json` for the Windows x64 Visual Studio workflow**

Write this file:

```json
{
  "version": 6,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 26,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "vs2022-x64",
      "displayName": "Visual Studio 2022 x64",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build/cmake-vs2022-x64",
      "architecture": "x64",
      "cacheVariables": {
        "CMAKE_POLICY_DEFAULT_CMP0091": "NEW"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "vs2022-x64-release",
      "configurePreset": "vs2022-x64",
      "configuration": "Release"
    }
  ]
}
```

- [ ] **Step 4: Run configure again and verify the root build now configures**

Run:

```powershell
cmake --preset vs2022-x64
```

Expected: PASS. Configure should complete without defining `proj2` or `shell` yet.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt CMakePresets.json
git commit -m "Add the root CMake entrypoint for Windows x64 parity migration"
```

### Task 2: Define `proj2` And `shell` Targets

**Files:**

- Modify: `E:/0_project/proj2_20260411/CMakeLists.txt`
- Test: `proj2` and `shell` build graph generation via `cmake --build`

- [ ] **Step 1: Verify a build currently fails because no targets exist**

Run:

```powershell
cmake --build E:/0_project/proj2_20260411/build/cmake-vs2022-x64 --config Release --target proj2
```

Expected: FAIL with an error equivalent to "target proj2 does not exist".

- [ ] **Step 2: Append the source lists and target definitions to `CMakeLists.txt`**

Append this block below the existing output-directory setup:

```cmake
set(PROJ2_SOURCES
    "${PROJECT_ROOT}/proj2/area.cpp"
    "${PROJECT_ROOT}/proj2/DetAlgorithm.cpp"
    "${PROJECT_ROOT}/proj2/detect.cpp"
    "${PROJECT_ROOT}/proj2/element.cpp"
    "${PROJECT_ROOT}/proj2/mylog.cpp"
    "${PROJECT_ROOT}/proj2/koujian.cpp"
    "${PROJECT_ROOT}/proj2/mycommon.cpp"
    "${PROJECT_ROOT}/proj2/proj2.cpp"
    "${PROJECT_ROOT}/proj2/tensorrt.cpp"
    "${PROJECT_ROOT}/proj2/yolov5Trt.cpp"
    "${PROJECT_ROOT}/proj2/proj2.rc"
)

set(SHELL_SOURCES
    "${PROJECT_ROOT}/shell/shell.cpp"
)

add_library(pugixml_vendor STATIC
    "${PUGIXML_DIR}/pugixml.cpp"
)

target_include_directories(pugixml_vendor
    PUBLIC
        "${PUGIXML_DIR}"
)

add_library(proj2 SHARED ${PROJ2_SOURCES})

target_include_directories(proj2
    PRIVATE
        "${PROJECT_ROOT}/proj2"
        "${PROJECT_ROOT}/public"
        "${BOOST_INCLUDE_DIR}"
        "${JSON_INCLUDE_DIR}"
        "${PUGIXML_DIR}"
        "${OPENCV_INCLUDE_DIR}"
        "${CUDA_INCLUDE_DIR}"
        "${TENSORRT_INCLUDE_DIR}"
)

target_compile_definitions(proj2
    PRIVATE
        _CRT_SECURE_NO_WARNINGS
        DC_EXPORTS
)

set_target_properties(proj2 PROPERTIES
    OUTPUT_NAME "proj2"
)

add_executable(shell ${SHELL_SOURCES})

target_include_directories(shell
    PRIVATE
        "${PROJECT_ROOT}/shell"
        "${PROJECT_ROOT}/public"
        "${PROJECT_ROOT}/proj2"
        "${BOOST_INCLUDE_DIR}"
        "${JSON_INCLUDE_DIR}"
        "${OPENCV_INCLUDE_DIR}"
)

set_target_properties(shell PROPERTIES
    OUTPUT_NAME "shell"
)
```

- [ ] **Step 3: Re-run configure to ensure the targets are registered**

Run:

```powershell
cmake --preset vs2022-x64
```

Expected: PASS with both `proj2` and `shell` known to the generated solution.

- [ ] **Step 4: Attempt a build and verify it now fails at linkage rather than target discovery**

Run:

```powershell
cmake --build E:/0_project/proj2_20260411/build/cmake-vs2022-x64 --config Release --target proj2 shell
```

Expected: FAIL with unresolved external or missing library errors, proving the target graph is now correct and the next remaining work is explicit linkage.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "Define proj2 and shell CMake targets"
```

### Task 3: Add Explicit Windows x64 Linkage

**Files:**

- Modify: `E:/0_project/proj2_20260411/CMakeLists.txt`
- Test: full Release build

- [ ] **Step 1: Add explicit library paths and link libraries for `proj2` and `shell`**

In `CMakeLists.txt`, append this block below the target definitions:

```cmake
target_link_directories(proj2
    PRIVATE
        "${OPENCV_LIB_DIR}"
        "${CUDA_LIB_DIR}"
        "${TENSORRT_LIB_DIR}"
)

target_link_directories(shell
    PRIVATE
        "${OPENCV_LIB_DIR}"
)

target_link_libraries(proj2
    PRIVATE
        pugixml_vendor
        opencv_world460.lib
        cudart.lib
        nvinfer.lib
        nvinfer_plugin.lib
        Rpcrt4.lib
)

target_link_libraries(shell
    PRIVATE
        opencv_world460.lib
)

if(MSVC)
    target_compile_options(proj2 PRIVATE /utf-8)
    target_compile_options(shell PRIVATE /utf-8)
endif()
```

- [ ] **Step 2: Run the full Release build and verify both targets compile**

Run:

```powershell
cmake --build E:/0_project/proj2_20260411/build/cmake-vs2022-x64 --config Release
```

Expected: PASS. `release/proj2.dll` and `release/shell.exe` should be created.

- [ ] **Step 3: If the build fails, resolve linkage by adjusting the explicit library list only**

Use this fallback rule:

- If TensorRT symbols are unresolved, add one library at a time from:

```cmake
nvinfer_dispatch.lib
nvonnxparser.lib
nvparsers.lib
```

- If CUDA runtime symbols are unresolved, add one library at a time from:

```cmake
cublas.lib
cublasLt.lib
cusolver.lib
cusparse.lib
```

Do not revert to wildcard `*.lib` linkage in CMake.

- [ ] **Step 4: Verify the release directory now contains the compiled artifacts**

Run:

```powershell
Get-ChildItem E:/0_project/proj2_20260411/release
```

Expected: contains at least `proj2.dll`, `shell.exe`, and the import library or debug artifacts produced by the generator.

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt
git commit -m "Add explicit Windows x64 linkage for the CMake targets"
```

### Task 4: Stage `config/` And Runtime DLLs Into `release/`

**Files:**

- Create: `E:/0_project/proj2_20260411/cmake/StageRuntime.cmake`
- Modify: `E:/0_project/proj2_20260411/CMakeLists.txt`
- Modify: `E:/0_project/proj2_20260411/.gitignore`
- Test: runtime layout under `E:/0_project/proj2_20260411/release`

- [ ] **Step 1: Add a helper script that copies the runtime directory contents**

Write `cmake/StageRuntime.cmake`:

```cmake
set(RELEASE_DIR "${PROJECT_ROOT}/release")
set(CONFIG_SOURCE_DIR "${PROJECT_ROOT}/config")
set(CONFIG_DEST_DIR "${RELEASE_DIR}/config")

file(MAKE_DIRECTORY "${RELEASE_DIR}")

if(EXISTS "${CONFIG_SOURCE_DIR}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CONFIG_SOURCE_DIR}" "${CONFIG_DEST_DIR}"
    )
endif()

set(RUNTIME_DLLS
    "${OPENCV_BIN_DIR}/opencv_world460.dll"
    "${OPENCV_BIN_DIR}/opencv_videoio_ffmpeg460_64.dll"
    "${OPENCV_BIN_DIR}/opencv_videoio_msmf460_64.dll"
    "${TENSORRT_LIB_DIR}/nvinfer.dll"
    "${TENSORRT_LIB_DIR}/nvinfer_plugin.dll"
    "${TENSORRT_LIB_DIR}/nvinfer_dispatch.dll"
    "${TENSORRT_LIB_DIR}/nvinfer_builder_resource.dll"
)

foreach(runtime_dll IN LISTS RUNTIME_DLLS)
    if(EXISTS "${runtime_dll}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${runtime_dll}" "${RELEASE_DIR}"
        )
    endif()
endforeach()
```

- [ ] **Step 2: Wire the staging script into both build targets**

Append this block to `CMakeLists.txt` after the link configuration:

```cmake
set(PROJECT_ROOT "${PROJECT_ROOT}" CACHE INTERNAL "")
set(OPENCV_BIN_DIR "${OPENCV_BIN_DIR}" CACHE INTERNAL "")
set(TENSORRT_LIB_DIR "${TENSORRT_LIB_DIR}" CACHE INTERNAL "")

add_custom_command(TARGET proj2 POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        -DPROJECT_ROOT="${PROJECT_ROOT}"
        -DOPENCV_BIN_DIR="${OPENCV_BIN_DIR}"
        -DTENSORRT_LIB_DIR="${TENSORRT_LIB_DIR}"
        -P "${PROJECT_ROOT}/cmake/StageRuntime.cmake"
)

add_custom_command(TARGET shell POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        -DPROJECT_ROOT="${PROJECT_ROOT}"
        -DOPENCV_BIN_DIR="${OPENCV_BIN_DIR}"
        -DTENSORRT_LIB_DIR="${TENSORRT_LIB_DIR}"
        -P "${PROJECT_ROOT}/cmake/StageRuntime.cmake"
)
```

- [ ] **Step 3: Ensure `.gitignore` excludes generated CMake build directories but keeps source CMake files tracked**

Confirm `.gitignore` contains at least:

```gitignore
build/
CMakeFiles/
CMakeCache.txt
cmake_install.cmake
compile_commands.json
```

If missing, add them without ignoring:

- `CMakeLists.txt`
- `CMakePresets.json`
- `cmake/`

- [ ] **Step 4: Rebuild and verify the staged runnable layout**

Run:

```powershell
cmake --build E:/0_project/proj2_20260411/build/cmake-vs2022-x64 --config Release
Get-ChildItem E:/0_project/proj2_20260411/release
Get-ChildItem E:/0_project/proj2_20260411/release/config
```

Expected:

- `release/shell.exe` exists
- `release/proj2.dll` exists
- `release/config/project.xml` exists
- `release/config/config_guang3.xml` exists
- copied OpenCV and TensorRT runtime DLLs exist in `release/`

- [ ] **Step 5: Commit**

```bash
git add CMakeLists.txt CMakePresets.json cmake/StageRuntime.cmake .gitignore
git commit -m "Stage runtime assets into the release directory after CMake builds"
```

### Task 5: Verify `shell.exe` Can Start Against The Staged Layout

**Files:**

- Test only: `E:/0_project/proj2_20260411/release`

- [ ] **Step 1: Launch the staged shell executable from the `release/` directory**

Run:

```powershell
Set-Location E:/0_project/proj2_20260411/release
.\shell.exe
```

Expected:

- the process starts
- it prompts for an input path
- it does not immediately fail with "proj2.dll load failed!"

- [ ] **Step 2: Do a smoke-path check with a known image or a known input folder**

Run with a real sample path when available:

```text
E:\data\sample.jpg
```

Expected:

- the program attempts DLL-backed inference
- if runtime model files are incomplete, the failure should now be a model/config/runtime issue, not a build-system issue

- [ ] **Step 3: Record the remaining runtime risk if `element2` still lacks an `.engine` file**

If the shell starts but inference fails due to a missing engine under `config/guang3/`, document that as a runtime asset gap and do not change the CMake build to hide it.

- [ ] **Step 4: Commit**

```bash
git add .
git commit -m "Verify the CMake-built release layout can launch the shell runtime"
```

## Self-Review

### Spec coverage

- Top-level CMake entrypoint: covered in Task 1
- `proj2` DLL target: covered in Task 2 and Task 3
- `shell` target: covered in Task 2 and Task 3
- vendored dependency resolution from `public/`: covered in Task 1 and Task 3
- `release/` runnable layout parity: covered in Task 1, Task 4, and Task 5
- `config/` copy into runtime output: covered in Task 4
- runtime DLL staging: covered in Task 4
- Windows x64 guardrails: covered in Task 1

### Placeholder scan

- No `TODO`, `TBD`, or "similar to above" placeholders remain.
- All created or modified file paths are explicit.
- All command steps include exact commands and expected results.

### Type and naming consistency

- Root build file path is consistently `E:/0_project/proj2_20260411/CMakeLists.txt`
- Preset name is consistently `vs2022-x64`
- Output directory is consistently `E:/0_project/proj2_20260411/release`
- Target names are consistently `proj2` and `shell`

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-04-12-cmake-migration-implementation.md`. Two execution options:**

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**

