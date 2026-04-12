# Jetson Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a first-stage Jetson AGX Orin / JetPack 5.1.2 native build and inference path while preserving the current Windows build and keeping the exported detection interface close to the existing `detect_process` contract.

**Architecture:** Use a Jetson-first minimum-intrusion strategy. Keep Windows and Jetson in the same codebase, introduce platform-aware export and utility handling with `#ifdef _WIN32` / `#ifdef __linux__`, add a Linux-native validation shell, and extend CMake into a dual Windows/Linux build rather than performing a broad architecture refactor in this stage.

**Tech Stack:** CMake, MSVC/Visual Studio 2019, Linux aarch64 on Jetson AGX Orin, JetPack 5.1.2, TensorRT, CUDA, OpenCV, nlohmann/json, pugixml, Boost.

---

## File Structure Lock-In

**Create:**

- `E:/0_project/proj2_20260411/shell/shell_jetson.cpp`

**Modify:**

- `E:/0_project/proj2_20260411/CMakeLists.txt`
- `E:/0_project/proj2_20260411/public/DetAlgorithm.h`
- `E:/0_project/proj2_20260411/proj2/detect.cpp`
- `E:/0_project/proj2_20260411/proj2/mylog.h`
- `E:/0_project/proj2_20260411/proj2/mylog.cpp`

**Avoid modifying unless forced by build/runtime errors:**

- `E:/0_project/proj2_20260411/proj2/area.cpp`
- `E:/0_project/proj2_20260411/proj2/koujian.cpp`
- `E:/0_project/proj2_20260411/proj2/element.cpp`
- `E:/0_project/proj2_20260411/proj2/tensorrt.cpp`
- `E:/0_project/proj2_20260411/proj2/yolov5Trt.cpp`

**Do not change in stage one:**

- Windows runtime behavior already validated in `release/`
- existing Windows test shell flow beyond the minimum needed to preserve builds

## Execution Strategy

This plan is ordered by “fastest route to first Jetson success”, not by architectural beauty:

1. make the exported interface dual-platform
2. add a Linux-native validation shell
3. remove or branch Windows-only path, directory, and UUID code
4. replace Windows-only ATL/MFC logging behavior on Linux
5. add a Linux/Jetson CMake branch
6. compile and run on Jetson

### Task 1: Make The Exported Detection Interface Cross-Platform

**Files:**

- Modify: `E:/0_project/proj2_20260411/public/DetAlgorithm.h`
- Test: Windows CMake configure/build still succeeds

- [ ] **Step 1: Write the failing portability check**

The current header hardcodes Windows DLL export syntax:

```cpp
extern "C" __declspec(dllexport) char* detect_process(char* file_Data, int* det_state, int* iPID = nullptr);
```

This will not compile for Linux shared-library export.

- [ ] **Step 2: Replace the hardcoded export with a cross-platform macro**

Update `public/DetAlgorithm.h` to this shape:

```cpp
#pragma once

#if defined(_WIN32)
#define DET_API __declspec(dllexport)
#elif defined(__linux__)
#define DET_API __attribute__((visibility("default")))
#else
#define DET_API
#endif

extern "C" DET_API char* detect_process(char* file_Data, int* det_state, int* iPID = nullptr);
```

Keep the rest of the file content and comments intact.

- [ ] **Step 3: Run Windows configure/build to verify no regression**

Run:

```powershell
cmake --preset vs2019-x64
cmake --build E:/0_project/proj2_20260411/build/cmake-vs2019-x64 --config Release --target proj2 shell
```

Expected: PASS on the current Windows-validated build path.

- [ ] **Step 4: Commit**

```bash
git add public/DetAlgorithm.h
git commit -m "Make the exported detect_process symbol cross-platform"
```

### Task 2: Add A Linux-Native Validation Shell

**Files:**

- Create: `E:/0_project/proj2_20260411/shell/shell_jetson.cpp`
- Test: source compiles in Linux target graph later

- [ ] **Step 1: Write the Linux test harness source**

Create `shell/shell_jetson.cpp` with a minimal Linux equivalent of the Windows shell behavior:

```cpp
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <nlohmann/json.hpp>

using DetectFn = char* (*)(char* file_Data, int* det_state, int* iPID);
namespace fs = std::filesystem;

static void test_one_jpg(const std::string& path, DetectFn fnDetect, int pid)
{
    nlohmann::json j;
    j["imagePath"] = path;
    std::string payload = j.dump();
    int det_state = 0;
    char* result = fnDetect(payload.data(), &det_state, &pid);
    std::cout << "path=" << path << " state=" << det_state << std::endl;
    if (result != nullptr)
    {
        std::cout << result << std::endl;
    }
}

int main()
{
    std::string input_path;
    int pid = 100;

    std::cout << "Please enter the jpg path: ";
    std::getline(std::cin, input_path);
    if (input_path.size() < 3)
    {
        return 0;
    }

    void* handle = dlopen("./libproj2.so", RTLD_NOW);
    if (!handle)
    {
        std::cerr << "libproj2.so load failed: " << dlerror() << std::endl;
        return 1;
    }

    auto fnDetect = reinterpret_cast<DetectFn>(dlsym(handle, "detect_process"));
    if (!fnDetect)
    {
        std::cerr << "dlsym(detect_process) failed: " << dlerror() << std::endl;
        dlclose(handle);
        return 1;
    }

    fs::path p(input_path);
    if (fs::is_regular_file(p))
    {
        test_one_jpg(input_path, fnDetect, pid);
    }

    dlclose(handle);
    return 0;
}
```

- [ ] **Step 2: Verify the file is syntax-shaped and consistent with the intended interface**

Run:

```powershell
Get-Content E:/0_project/proj2_20260411/shell/shell_jetson.cpp
```

Expected: the file uses `dlopen` / `dlsym`, passes JSON with `imagePath`, and calls `detect_process`.

- [ ] **Step 3: Commit**

```bash
git add shell/shell_jetson.cpp
git commit -m "Add a Linux-native validation shell for Jetson"
```

### Task 3: Replace Windows-Only Path, Directory, And UUID Logic In `detect.cpp`

**Files:**

- Modify: `E:/0_project/proj2_20260411/proj2/detect.cpp`
- Test: Windows build still passes after branching

- [ ] **Step 1: Add platform headers and helpers for Linux**

Near the includes in `proj2/detect.cpp`, add Linux branches for:

```cpp
#ifdef __linux__
#include <unistd.h>
#include <uuid/uuid.h>
#endif
```

Keep the existing Windows includes under `_WIN32` if needed.

- [ ] **Step 2: Replace `GetModuleFileName`-based executable directory discovery with a cross-platform helper**

Add a helper like:

```cpp
static std::string get_runtime_root()
{
#ifdef _WIN32
    wchar_t wcFilePath[255];
    GetModuleFileNameW(NULL, wcFilePath, 255);
    char filePath[255];
    size_t len = std::wcstombs(nullptr, wcFilePath, 0) + 1;
    std::wcstombs(filePath, wcFilePath, len);
    (strrchr(filePath, '\\'))[1] = 0;
    return std::string(filePath);
#elif defined(__linux__)
    char exePath[4096] = {0};
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len <= 0)
    {
        return fs::current_path().string() + "/";
    }
    exePath[len] = '\0';
    std::string path(exePath);
    auto slash = path.find_last_of('/');
    if (slash != std::string::npos)
    {
        path = path.substr(0, slash + 1);
    }
    return path;
#else
    return fs::current_path().string() + "/";
#endif
}
```

Then replace the inline runtime-root logic in `initrt()` with `std::string sexeFilePath = get_runtime_root();`.

- [ ] **Step 3: Replace `_access` / `_mkdir` helper behavior with `std::filesystem` where practical**

The global helper `CreateDird` should be rewritten to:

```cpp
void CreateDird(const std::string& directoryPath)
{
    std::error_code ec;
    fs::create_directories(fs::path(directoryPath), ec);
}
```

Do not keep the Windows-only `_access` / `_mkdir` traversal in this helper.

- [ ] **Step 4: Replace Windows RPC UUID generation with a Linux branch**

In the UUID generation section of `detect_process(imgInfo, ...)`, preserve the Windows logic and add a Linux branch:

```cpp
#ifdef _WIN32
    UUID uuid;
    UuidCreate(&uuid);
    unsigned char* pBuf;
    UuidToStringA(&uuid, &pBuf);
    std::string uuid_str(reinterpret_cast<char*>(pBuf));
    RpcStringFreeA(&pBuf);
#elif defined(__linux__)
    uuid_t uuid;
    uuid_generate(uuid);
    char uuid_buf[37] = {0};
    uuid_unparse_lower(uuid, uuid_buf);
    std::string uuid_str(uuid_buf);
#endif
```

Keep the existing hyphen-removal behavior afterward so the output format remains close to the current contract:

```cpp
uuid_str.erase(std::remove(uuid_str.begin(), uuid_str.end(), '-'), uuid_str.end());
```

- [ ] **Step 5: Re-run Windows build to verify no regression from the new `#ifdef` logic**

Run:

```powershell
cmake --preset vs2019-x64
cmake --build E:/0_project/proj2_20260411/build/cmake-vs2019-x64 --config Release --target proj2 shell
```

Expected: PASS on Windows.

- [ ] **Step 6: Commit**

```bash
git add proj2/detect.cpp
git commit -m "Add Linux-compatible runtime path and UUID handling"
```

### Task 4: Add A Linux Logging Branch In `mylog.h` And `mylog.cpp`

**Files:**

- Modify: `E:/0_project/proj2_20260411/proj2/mylog.h`
- Modify: `E:/0_project/proj2_20260411/proj2/mylog.cpp`
- Test: Windows build still passes

- [ ] **Step 1: Keep the current Windows implementation under `_WIN32`**

Wrap the existing ATL/MFC-heavy declarations and helpers so they remain the Windows path.

- [ ] **Step 2: Add a Linux-compatible minimal logging implementation**

For Linux, provide a minimal branch that preserves the public helper shape used elsewhere:

In `mylog.h`, add a Linux branch with signatures like:

```cpp
#ifdef __linux__
#include <string>

inline void OnSetLogLevel(int ilogLevel, int iPID);
inline void ShowLog(int iLOG_LEVEL,
    const std::string& sInfo,
    const std::string& sPath,
    int iPrintfFlage,
    const std::string& filepath,
    const std::string& funname,
    const std::string& slinenum);
#endif
```

And implement them so they:

- write to the existing log file path scheme when possible
- print to stdout/stderr when requested
- avoid any use of `CString`, `ATLComTime.h`, `CA2T`, or `CW2A`

The Linux branch can use:

- `std::ofstream`
- `std::chrono`
- `std::filesystem`
- plain `std::string`

- [ ] **Step 3: Keep the call sites stable enough to avoid broader churn**

If necessary, introduce Linux-side helper overloads or macros so the rest of the code can continue calling `ShowLog(...)` with minimal edits.

The key rule is:

- do not spread ATL replacement logic across the whole codebase
- contain it in the logging layer as much as possible

- [ ] **Step 4: Re-run the Windows build**

Run:

```powershell
cmake --preset vs2019-x64
cmake --build E:/0_project/proj2_20260411/build/cmake-vs2019-x64 --config Release --target proj2 shell
```

Expected: PASS on Windows after introducing the Linux branch.

- [ ] **Step 5: Commit**

```bash
git add proj2/mylog.h proj2/mylog.cpp
git commit -m "Add a Linux-compatible logging path beside the Windows ATL implementation"
```

### Task 5: Extend CMake To Build Windows And Jetson/Linux Variants

**Files:**

- Modify: `E:/0_project/proj2_20260411/CMakeLists.txt`
- Test: Windows build still passes; Linux branch configures on Jetson

- [ ] **Step 1: Keep the current Windows branch intact**

Preserve the working Windows path that now builds and stages `release/`.

- [ ] **Step 2: Add a Linux branch that builds `libproj2.so` and `shell_jetson`**

In `CMakeLists.txt`, structure the build by platform:

- Windows branch:
  - existing `proj2`
  - existing `shell`
  - existing runtime staging

- Linux branch:
  - `proj2` as a shared library
  - `shell_jetson` as an executable
  - no Windows-only staging logic

The Linux branch should:

- include `proj2` sources
- include `shell/shell_jetson.cpp`
- link `dl`
- link `pthread` if needed
- link Jetson-native OpenCV
- link Jetson-native TensorRT
- link Jetson-native CUDA
- link `uuid`

- [ ] **Step 3: Replace Windows vendored dependency assumptions in the Linux branch**

For Linux/Jetson, do not use:

- `public/CUDA11.8`
- `public/TensorRT-8.5.2.2`
- `public/OpenCV4.6.0`

Instead, the Linux branch should use:

- native include/library discovery
- explicit fallback paths suitable for JetPack 5.1.2 only if necessary

At minimum, include one of these strategies:

```cmake
find_package(OpenCV REQUIRED)
find_library(TENSORRT_LIB nvinfer REQUIRED)
find_library(TENSORRT_PLUGIN_LIB nvinfer_plugin REQUIRED)
find_library(TENSORRT_ONNX_LIB nvonnxparser REQUIRED)
find_library(NVPARSERS_LIB nvparsers)
find_library(CUDART_LIB cudart REQUIRED)
find_library(UUID_LIB uuid REQUIRED)
```

- [ ] **Step 4: Add Linux-only compile definitions and export behavior**

The Linux branch should define any macro needed by the cross-platform export header and compile cleanly with `-fPIC` where appropriate.

- [ ] **Step 5: Re-run Windows build after the dual-platform CMake changes**

Run:

```powershell
cmake --preset vs2019-x64
cmake --build E:/0_project/proj2_20260411/build/cmake-vs2019-x64 --config Release --target proj2 shell
```

Expected: Windows still passes after adding the Linux branch.

- [ ] **Step 6: On Jetson, run configure/build for the Linux path**

Run on Jetson:

```bash
cmake -S /path/to/proj2_20260411 -B /path/to/proj2_20260411/build-jetson -DCMAKE_BUILD_TYPE=Release
cmake --build /path/to/proj2_20260411/build-jetson -j
```

Expected: a Linux shared library and Linux validation shell are produced.

- [ ] **Step 7: Commit**

```bash
git add CMakeLists.txt shell/shell_jetson.cpp public/DetAlgorithm.h proj2/detect.cpp proj2/mylog.h proj2/mylog.cpp
git commit -m "Add the first-stage dual-platform Windows and Jetson build path"
```

### Task 6: Validate Jetson Runtime With A Real Image

**Files:**

- Test only on Jetson runtime output

- [ ] **Step 1: Arrange the runtime layout on Jetson**

Place:

- `libproj2.so`
- `shell_jetson`
- `config/`
- Jetson-compatible `.engine` files

in a consistent runtime directory.

- [ ] **Step 2: Launch the Linux validation shell**

Run on Jetson:

```bash
cd /path/to/runtime
./shell_jetson
```

Expected: prompt for an image path and successfully `dlopen("./libproj2.so")`.

- [ ] **Step 3: Run a real image through the interface**

Provide a real Jetson-accessible image path.

Expected:

- the library loads config
- models initialize
- inference runs
- the JSON-oriented result is returned or logged

- [ ] **Step 4: If inference fails due to TensorRT engine mismatch, regenerate engines on Jetson**

Use the Jetson-compatible TensorRT environment and existing ONNX files to regenerate `.engine` files for the Linux runtime.

- [ ] **Step 5: Capture the validation evidence**

Record:

- configure success
- build success
- shell launch success
- one real image inference result

- [ ] **Step 6: Commit**

```bash
git add .
git commit -m "Validate the first-stage Jetson runtime path on device"
```

## Self-Review

### Spec coverage

- preserve Windows support: covered in Tasks 1, 3, 4, and 5
- native Jetson build: covered in Task 5
- preserve interface shape: covered in Task 1
- use minimum-intrusion `#ifdef` strategy: covered in Tasks 3 and 4
- add Linux-native validation shell: covered in Task 2
- replace Windows-only platform blockers first: covered in Tasks 3 and 4
- handle TensorRT/CUDA/OpenCV as Jetson-native dependencies: covered in Task 5 and Task 6

### Placeholder scan

- No `TODO`, `TBD`, or “similar to above” placeholders remain.
- Every task contains explicit file paths.
- Every code-changing task includes actual code or exact implementation direction.

### Type consistency

- exported function name remains `detect_process`
- Linux validation shell consistently assumes `libproj2.so`
- platform split is consistently `_WIN32` vs `__linux__`

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-04-13-jetson-support-implementation.md`. Two execution options:**

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**

