# Jetson Performance Profiling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build config-only performance profiling that writes comparable Windows and Jetson CSV timing data before inference optimization begins.

**Architecture:** Put the profiler writer inside the detection library so `proj2.dll` / `libproj2.so` owns CSV state. Shell executables resolve optional profiling exports and report `file_total` through the library, while internal detection code uses scoped timers directly.

**Tech Stack:** C++17, CMake, pugixml, std::filesystem, std::chrono, Windows DLL exports, Linux shared-object exports, existing shell dynamic loading.

---

## File Structure

Create:

- `proj2/perf_profiler.h`: public profiler API used by detection code and tests.
- `proj2/perf_profiler.cpp`: config parsing, scoped timing, CSV writing, console summaries, thread-local image context.
- `tests/perf_profiler_test.cpp`: unit tests for config parsing, disabled behavior, CSV header and event rows.

Modify:

- `CMakeLists.txt`: add profiler source to `proj2`, add `perf_profiler_tests`, link with `pugixml_vendor`.
- `proj2/DetAlgorithm.cpp`: export C ABI functions for shell configuration and `file_total` recording; add `detect_process_total` timer.
- `proj2/detect.cpp`: configure profiler from `project.xml` as fallback; add `main_process_total`, `read_image`, `save_json`, and `save_result_image` timers.
- `proj2/detect.h`: include profiler context member if needed; otherwise no public shape change.
- `proj2/area.cpp`: add `area0` / `area1` `process_total` timing and ROI count.
- `proj2/koujian.cpp`: add `detail0` / `detail1` `process_total` timing and ROI count.
- `proj2/element.cpp`: add `elementN` `process_total` timing and ROI count.
- `shell/shell.cpp`: resolve optional profiler exports and record `file_total`.
- `shell/shell_jetson.cpp`: same as Windows shell with Linux `dlsym`.
- `config/project.xml`: add `<perf_profile enable="1" detail="stage" output_dir="perf"/>`.
- `config/config_guang3.xml`: set model `<state state="1"/>` entries for the profiling run.

Do not add model-internal CUDA timers in the first implementation commit. Stage profiling must land and be verified first.

---

### Task 1: Add Profiler Core And Unit Tests

**Files:**
- Create: `proj2/perf_profiler.h`
- Create: `proj2/perf_profiler.cpp`
- Create: `tests/perf_profiler_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing profiler unit test**

Create `tests/perf_profiler_test.cpp`:

```cpp
#include "perf_profiler.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static void require(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}

static std::string read_file(const fs::path& path)
{
    std::ifstream in(path);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

int main()
{
    const fs::path root = fs::temp_directory_path() / "proj2_perf_profiler_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "config");

    const fs::path project_xml = root / "config" / "project.xml";
    {
        std::ofstream out(project_xml);
        out << "<root><pthreading>"
            << "<perf_profile enable=\"1\" detail=\"stage\" output_dir=\"perf\"/>"
            << "</pthreading></root>";
    }

    perf::reset_for_tests();
    require(perf::configure_from_project_xml(project_xml, root), "enabled profiler config should parse");
    require(perf::enabled(), "profiler should be enabled");
    require(perf::detail() == perf::Detail::Stage, "detail should be stage");

    {
        perf::ImageScope image("E1/test.jpg", 7);
        perf::ScopedTimer timer("stage", "detect", "read_image");
    }
    perf::record_event("stage", "detect", "save_json", 3, 1, 2, -1, "");
    perf::record_file_total("E1/test.jpg", 7, 42, 1, 2);
    perf::flush();

    const fs::path csv_path = perf::csv_path_for_tests();
    require(fs::exists(csv_path), "CSV file should exist");
    const std::string csv = read_file(csv_path);
    require(csv.find("run_id,timestamp,platform,image_path,thread_id,level,component,stage,duration_ms,state,flaws,roi_count,extra") != std::string::npos,
            "CSV should contain header");
    require(csv.find(",stage,detect,read_image,") != std::string::npos,
            "CSV should contain read_image event");
    require(csv.find(",stage,detect,save_json,3,1,2,") != std::string::npos,
            "CSV should contain save_json event");
    require(csv.find(",stage,shell,file_total,42,1,2,") != std::string::npos,
            "CSV should contain file_total event");

    {
        std::ofstream out(project_xml);
        out << "<root><pthreading>"
            << "<perf_profile enable=\"0\" detail=\"stage\" output_dir=\"perf_disabled\"/>"
            << "</pthreading></root>";
    }

    perf::reset_for_tests();
    require(perf::configure_from_project_xml(project_xml, root), "disabled profiler config should parse");
    require(!perf::enabled(), "profiler should be disabled");
    perf::record_event("stage", "detect", "read_image", 1, 0, 0, -1, "");
    require(perf::csv_path_for_tests().empty(), "disabled profiler should not expose a CSV path");

    fs::remove_all(root, ec);
    return 0;
}
```

- [ ] **Step 2: Add the test target and verify it fails before implementation**

Modify `CMakeLists.txt` near the existing `batch_summary_tests` target:

```cmake
add_executable(perf_profiler_tests
    "${PROJECT_ROOT}/tests/perf_profiler_test.cpp"
    "${PROJECT_ROOT}/proj2/perf_profiler.cpp"
)

target_include_directories(perf_profiler_tests
    PRIVATE
        "${PROJECT_ROOT}/proj2"
        "${PUGIXML_DIR}"
)

target_link_libraries(perf_profiler_tests
    PRIVATE
        pugixml_vendor
)

add_test(NAME perf_profiler_tests COMMAND perf_profiler_tests)
```

Run:

```powershell
cmake --build --preset dev-relwithdebinfo-tests --target perf_profiler_tests
```

Expected: build fails because `proj2/perf_profiler.h` and `proj2/perf_profiler.cpp` do not exist.

- [ ] **Step 3: Create the profiler header**

Create `proj2/perf_profiler.h`:

```cpp
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace perf
{

enum class Detail
{
    Off,
    Stage,
    Model
};

bool configure_from_project_xml(const std::filesystem::path& project_xml_path,
                                const std::filesystem::path& runtime_root);
void configure_disabled();
bool enabled();
Detail detail();
bool model_detail_enabled();

void set_image_context(const std::string& image_path, int thread_id);
void clear_image_context();

void record_event(const char* level,
                  const char* component,
                  const char* stage,
                  long long duration_ms,
                  int state = -9999,
                  int flaws = -9999,
                  int roi_count = -1,
                  const std::string& extra = std::string());

void record_file_total(const std::string& image_path,
                       int thread_id,
                       long long duration_ms,
                       int state,
                       int flaws);

void flush();

class ImageScope
{
public:
    ImageScope(const std::string& image_path, int thread_id);
    ~ImageScope();

    ImageScope(const ImageScope&) = delete;
    ImageScope& operator=(const ImageScope&) = delete;

private:
    std::string previous_image_path_;
    int previous_thread_id_ = -1;
    bool previous_valid_ = false;
};

class ScopedTimer
{
public:
    ScopedTimer(const char* level,
                const char* component,
                const char* stage,
                int state = -9999,
                int flaws = -9999,
                int roi_count = -1,
                const std::string& extra = std::string());
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    bool active_ = false;
    const char* level_ = "";
    const char* component_ = "";
    const char* stage_ = "";
    int state_ = -9999;
    int flaws_ = -9999;
    int roi_count_ = -1;
    std::string extra_;
    std::chrono::steady_clock::time_point start_;
};

void reset_for_tests();
std::filesystem::path csv_path_for_tests();

} // namespace perf
```

- [ ] **Step 4: Create the profiler implementation**

Create `proj2/perf_profiler.cpp` with these concrete behaviors:

```cpp
#include "perf_profiler.h"

#include "pugixml.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace perf
{
namespace
{

struct ImageContext
{
    std::string image_path;
    int thread_id = -1;
    bool valid = false;
};

struct State
{
    bool enabled = false;
    Detail detail = Detail::Off;
    fs::path output_dir;
    fs::path csv_path;
    std::string run_id;
    std::string platform;
    bool header_written = false;
    std::mutex mutex;
};

State& state()
{
    static State value;
    return value;
}

thread_local ImageContext image_context;

std::string two_digits(int value)
{
    std::ostringstream out;
    out << std::setw(2) << std::setfill('0') << value;
    return out.str();
}

std::string now_compact()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_value{};
#ifdef _WIN32
    localtime_s(&tm_value, &t);
#else
    localtime_r(&t, &tm_value);
#endif
    std::ostringstream out;
    out << (tm_value.tm_year + 1900)
        << two_digits(tm_value.tm_mon + 1)
        << two_digits(tm_value.tm_mday)
        << "_"
        << two_digits(tm_value.tm_hour)
        << two_digits(tm_value.tm_min)
        << two_digits(tm_value.tm_sec);
    return out.str();
}

std::string now_iso()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_value{};
#ifdef _WIN32
    localtime_s(&tm_value, &t);
#else
    localtime_r(&t, &tm_value);
#endif
    std::ostringstream out;
    out << (tm_value.tm_year + 1900) << "-"
        << two_digits(tm_value.tm_mon + 1) << "-"
        << two_digits(tm_value.tm_mday) << "T"
        << two_digits(tm_value.tm_hour) << ":"
        << two_digits(tm_value.tm_min) << ":"
        << two_digits(tm_value.tm_sec);
    return out.str();
}

std::string platform_name()
{
#ifdef _WIN32
    return "windows";
#elif defined(__linux__)
#if defined(__aarch64__)
    return "jetson";
#else
    return "linux";
#endif
#else
    return "unknown";
#endif
}

std::string csv_escape(const std::string& value)
{
    if (value.find_first_of(",\"\n\r") == std::string::npos)
    {
        return value;
    }

    std::string escaped = "\"";
    for (char ch : value)
    {
        if (ch == '"')
        {
            escaped += "\"\"";
        }
        else
        {
            escaped += ch;
        }
    }
    escaped += "\"";
    return escaped;
}

Detail parse_detail(const std::string& raw)
{
    if (raw == "model")
    {
        return Detail::Model;
    }
    return Detail::Stage;
}

void write_header_if_needed(std::ofstream& out)
{
    State& s = state();
    if (!s.header_written)
    {
        out << "run_id,timestamp,platform,image_path,thread_id,level,component,stage,duration_ms,state,flaws,roi_count,extra\n";
        s.header_written = true;
    }
}

std::string int_or_empty(int value, int empty_sentinel)
{
    if (value == empty_sentinel)
    {
        return "";
    }
    return std::to_string(value);
}

} // namespace

bool configure_from_project_xml(const fs::path& project_xml_path, const fs::path& runtime_root)
{
    pugi::xml_document doc;
    const pugi::xml_parse_result parse_result = doc.load_file(project_xml_path.string().c_str(), pugi::parse_default, pugi::encoding_utf8);
    if (!parse_result)
    {
        configure_disabled();
        return false;
    }

    const pugi::xml_node node = doc.child("root").child("pthreading").child("perf_profile");
    if (node.empty() || node.attribute("enable").as_int(0) != 1)
    {
        configure_disabled();
        return true;
    }

    std::string output_dir = node.attribute("output_dir").as_string("perf");
    if (output_dir.empty())
    {
        output_dir = "perf";
    }

    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.enabled = true;
    s.detail = parse_detail(node.attribute("detail").as_string("stage"));
    s.output_dir = fs::path(output_dir);
    if (s.output_dir.is_relative())
    {
        s.output_dir = runtime_root / s.output_dir;
    }
    s.run_id = now_compact();
    s.platform = platform_name();
    std::error_code ec;
    fs::create_directories(s.output_dir, ec);
    s.csv_path = s.output_dir / ("perf_" + s.run_id + ".csv");
    s.header_written = fs::exists(s.csv_path);
    return true;
}

void configure_disabled()
{
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.enabled = false;
    s.detail = Detail::Off;
    s.output_dir.clear();
    s.csv_path.clear();
    s.run_id.clear();
    s.platform = platform_name();
    s.header_written = false;
}

bool enabled()
{
    return state().enabled;
}

Detail detail()
{
    return state().detail;
}

bool model_detail_enabled()
{
    return enabled() && detail() == Detail::Model;
}

void set_image_context(const std::string& image_path, int thread_id)
{
    image_context.image_path = image_path;
    image_context.thread_id = thread_id;
    image_context.valid = true;
}

void clear_image_context()
{
    image_context = ImageContext{};
}

void record_event(const char* level,
                  const char* component,
                  const char* stage,
                  long long duration_ms,
                  int state_value,
                  int flaws,
                  int roi_count,
                  const std::string& extra)
{
    State& s = state();
    if (!s.enabled)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(s.mutex);
    std::ofstream out(s.csv_path, std::ios::app);
    if (!out.is_open())
    {
        std::cerr << "[perf] cannot open CSV: " << s.csv_path.string() << std::endl;
        return;
    }

    write_header_if_needed(out);
    out << csv_escape(s.run_id) << ","
        << csv_escape(now_iso()) << ","
        << csv_escape(s.platform) << ","
        << csv_escape(image_context.valid ? image_context.image_path : "") << ","
        << (image_context.valid ? std::to_string(image_context.thread_id) : "") << ","
        << csv_escape(level ? level : "") << ","
        << csv_escape(component ? component : "") << ","
        << csv_escape(stage ? stage : "") << ","
        << duration_ms << ","
        << int_or_empty(state_value, -9999) << ","
        << int_or_empty(flaws, -9999) << ","
        << (roi_count >= 0 ? std::to_string(roi_count) : "") << ","
        << csv_escape(extra) << "\n";
}

void record_file_total(const std::string& image_path, int thread_id, long long duration_ms, int state_value, int flaws)
{
    ImageScope image(image_path, thread_id);
    record_event("stage", "shell", "file_total", duration_ms, state_value, flaws, -1, "");
    if (enabled())
    {
        std::cout << "[perf] image=" << image_path
                  << " total=" << duration_ms << "ms"
                  << " state=" << state_value
                  << " flaws=" << flaws << std::endl;
    }
}

void flush()
{
}

ImageScope::ImageScope(const std::string& image_path, int thread_id)
{
    previous_image_path_ = image_context.image_path;
    previous_thread_id_ = image_context.thread_id;
    previous_valid_ = image_context.valid;
    set_image_context(image_path, thread_id);
}

ImageScope::~ImageScope()
{
    image_context.image_path = previous_image_path_;
    image_context.thread_id = previous_thread_id_;
    image_context.valid = previous_valid_;
}

ScopedTimer::ScopedTimer(const char* level,
                         const char* component,
                         const char* stage,
                         int state_value,
                         int flaws,
                         int roi_count,
                         const std::string& extra)
    : active_(enabled()),
      level_(level),
      component_(component),
      stage_(stage),
      state_(state_value),
      flaws_(flaws),
      roi_count_(roi_count),
      extra_(extra),
      start_(std::chrono::steady_clock::now())
{
}

ScopedTimer::~ScopedTimer()
{
    if (!active_)
    {
        return;
    }

    const auto end = std::chrono::steady_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
    record_event(level_, component_, stage_, duration, state_, flaws_, roi_count_, extra_);
}

void reset_for_tests()
{
    configure_disabled();
    clear_image_context();
}

fs::path csv_path_for_tests()
{
    return state().csv_path;
}

} // namespace perf
```

- [ ] **Step 5: Add profiler source to the main library**

Modify the `PROJ2_SOURCES` list in `CMakeLists.txt`:

```cmake
set(PROJ2_SOURCES
    "${PROJECT_ROOT}/proj2/area.cpp"
    "${PROJECT_ROOT}/proj2/DetAlgorithm.cpp"
    "${PROJECT_ROOT}/proj2/detect.cpp"
    "${PROJECT_ROOT}/proj2/element.cpp"
    "${PROJECT_ROOT}/proj2/mylog.cpp"
    "${PROJECT_ROOT}/proj2/koujian.cpp"
    "${PROJECT_ROOT}/proj2/mycommon.cpp"
    "${PROJECT_ROOT}/proj2/perf_profiler.cpp"
    "${PROJECT_ROOT}/proj2/proj2.cpp"
    "${PROJECT_ROOT}/proj2/tensorrt.cpp"
    "${PROJECT_ROOT}/proj2/yolov5Trt.cpp"
)
```

Ensure existing `target_include_directories(proj2 ...)` includes `proj2` and `PUGIXML_DIR`. If `proj2` is missing from either Windows or Linux branch, add:

```cmake
"${PROJECT_ROOT}/proj2"
```

- [ ] **Step 6: Run the profiler unit test**

Run:

```powershell
cmake --build --preset dev-relwithdebinfo-tests --target perf_profiler_tests
.\release\perf_profiler_tests.exe
```

Expected: process exits `0`.

- [ ] **Step 7: Commit Task 1**

```bash
git add CMakeLists.txt proj2/perf_profiler.h proj2/perf_profiler.cpp tests/perf_profiler_test.cpp
git commit -m "Add config-driven profiling writer" -m "Profiler state lives in the detection library so Windows and Jetson can emit comparable CSV timing rows through one implementation." -m "Constraint: Profiling is configuration-only" -m "Confidence: high" -m "Scope-risk: moderate" -m "Tested: perf_profiler_tests"
```

---

### Task 2: Export Library Profiling Functions And Configure From Shell

**Files:**
- Modify: `proj2/DetAlgorithm.cpp`
- Modify: `shell/shell.cpp`
- Modify: `shell/shell_jetson.cpp`

- [ ] **Step 1: Add exported profiler functions to the detection library**

Modify `proj2/DetAlgorithm.cpp` includes:

```cpp
#include "perf_profiler.h"
```

Add these exports after the existing log-capture exports:

```cpp
extern "C" PROJ2_API void configure_perf_profile(const char* projectXmlPath, const char* runtimeRoot)
{
    if (projectXmlPath == nullptr || runtimeRoot == nullptr)
    {
        perf::configure_disabled();
        return;
    }

    perf::configure_from_project_xml(projectXmlPath, runtimeRoot);
}

extern "C" PROJ2_API void record_perf_file_total(const char* imagePath,
                                                 int threadId,
                                                 long long durationMs,
                                                 int state,
                                                 int flaws)
{
    if (imagePath == nullptr)
    {
        return;
    }

    perf::record_file_total(imagePath, threadId, durationMs, state, flaws);
}
```

- [ ] **Step 2: Add `detect_process_total` timer**

In `proj2/DetAlgorithm.cpp::detect_process`, after the null checks and before `Cdetect` construction:

```cpp
    perf::ScopedTimer detect_timer("stage", "detect", "detect_process_total");
```

Keep the existing `ShowLog` summary. Do not replace the existing log output.

- [ ] **Step 3: Resolve profiling exports in Windows shell**

In `shell/shell.cpp`, add typedefs near existing dynamic function typedefs:

```cpp
using ConfigurePerfProfileFn = void (*)(const char*, const char*);
using RecordPerfFileTotalFn = void (*)(const char*, int, long long, int, int);
static ConfigurePerfProfileFn fnConfigurePerfProfile = nullptr;
static RecordPerfFileTotalFn fnRecordPerfFileTotal = nullptr;
```

After `fnDetect` is resolved successfully:

```cpp
    fnConfigurePerfProfile = reinterpret_cast<ConfigurePerfProfileFn>(GetProcAddress(hDll, "configure_perf_profile"));
    fnRecordPerfFileTotal = reinterpret_cast<RecordPerfFileTotalFn>(GetProcAddress(hDll, "record_perf_file_total"));
```

After `projectXmlPath` is known and before processing paths:

```cpp
    if (fnConfigurePerfProfile != nullptr)
    {
        fnConfigurePerfProfile(projectXmlPath.string().c_str(), executable_dir.string().c_str());
    }
```

Use the existing variable that points to the executable directory. If the current code only has `projectXmlPath`, compute:

```cpp
    const fs::path executable_dir = get_executable_dir();
```

using the existing project helper rather than adding a second path resolver.

- [ ] **Step 4: Record `file_total` in Windows JPG and JSON processors**

In `shell/shell.cpp::process_one_jpg_file`, after `duration` is computed:

```cpp
    const int flaws = (det_state == 1 && outData != nullptr) ? 1 : 0;
    if (fnRecordPerfFileTotal != nullptr)
    {
        fnRecordPerfFileTotal(sInpath.c_str(), iPID, duration.count(), det_state, flaws);
    }
```

In `process_one_json_file`, use the same block with its path variable.

- [ ] **Step 5: Resolve profiling exports in Jetson shell**

In `shell/shell_jetson.cpp`, add typedefs near existing function typedefs:

```cpp
using ConfigurePerfProfileFn = void (*)(const char*, const char*);
using RecordPerfFileTotalFn = void (*)(const char*, int, long long, int, int);
static ConfigurePerfProfileFn fnConfigurePerfProfile = nullptr;
static RecordPerfFileTotalFn fnRecordPerfFileTotal = nullptr;
```

After `detect_process` is resolved:

```cpp
    fnConfigurePerfProfile = reinterpret_cast<ConfigurePerfProfileFn>(dlsym(handle, "configure_perf_profile"));
    fnRecordPerfFileTotal = reinterpret_cast<RecordPerfFileTotalFn>(dlsym(handle, "record_perf_file_total"));
```

After `project_xml_path` is known:

```cpp
        if (fnConfigurePerfProfile != nullptr)
        {
            fnConfigurePerfProfile(project_xml_path.string().c_str(), executable_dir.string().c_str());
        }
```

- [ ] **Step 6: Record `file_total` in Jetson JPG and JSON processors**

In `shell/shell_jetson.cpp::process_one_jpg_file`, after `duration` is computed:

```cpp
    const int flaws = (det_state == 1 && outData != nullptr) ? 1 : 0;
    if (fnRecordPerfFileTotal != nullptr)
    {
        fnRecordPerfFileTotal(path.c_str(), pid, duration.count(), det_state, flaws);
    }
```

Add the same block in `process_one_json_file`.

- [ ] **Step 7: Build shell and library**

Run:

```powershell
cmake --build --preset dev-relwithdebinfo-tests --target shell
```

Expected: `release/proj2.dll` and `release/shell.exe` build successfully.

- [ ] **Step 8: Commit Task 2**

```bash
git add proj2/DetAlgorithm.cpp shell/shell.cpp shell/shell_jetson.cpp
git commit -m "Route shell profiling through detection library" -m "The shell measures file-level elapsed time, but the library owns CSV state and writes the event through optional exports." -m "Constraint: Existing shells must still run if profiler exports are unavailable" -m "Rejected: Compile a separate profiler writer into shell | would split CSV ownership between exe and library" -m "Confidence: high" -m "Scope-risk: moderate" -m "Tested: cmake build shell"
```

---

### Task 3: Add Detection Stage Timers

**Files:**
- Modify: `proj2/detect.cpp`
- Modify: `proj2/detect.h` only if a helper member becomes necessary

- [ ] **Step 1: Include profiler header**

In `proj2/detect.cpp`:

```cpp
#include "perf_profiler.h"
```

- [ ] **Step 2: Add fallback profiler configuration during initialization**

In `Cdetect::initrt`, after `projectXml_path` is computed and verified:

```cpp
    perf::configure_from_project_xml(projectXml_path, runtime_root);
```

This is a fallback for direct library use. It is harmless when shell already configured the profiler because it uses the same XML and runtime root.

- [ ] **Step 3: Add `main_process_total` timer**

At the start of `Cdetect::main_process`, after local null-sensitive variables are safe to use:

```cpp
    perf::ScopedTimer main_timer("stage", "detect", "main_process_total");
```

Keep existing initialization behavior unchanged.

- [ ] **Step 4: Set image context and time image read**

In `Cdetect::in_process`, after `param.jpgpath` is known and before `cv::imread`:

```cpp
    perf::ImageScope image_scope(param.jpgpath, m_iPID);
```

Replace the direct read:

```cpp
    cv::Mat image = cv::imread(param.jpgpath.c_str(), 1);
```

with:

```cpp
    cv::Mat image;
    {
        perf::ScopedTimer timer("stage", "detect", "read_image");
        image = cv::imread(param.jpgpath.c_str(), 1);
    }
```

- [ ] **Step 5: Time JSON output**

Locate the code that writes `m_result_json` / `_result.json` in `Cdetect::in_process` or the helper it calls. Wrap only the file-writing block:

```cpp
    {
        perf::ScopedTimer timer("stage", "detect", "save_json");
        // existing JSON file write block stays here
    }
```

If JSON output is skipped by config, do not emit a fake zero-duration row.

- [ ] **Step 6: Time result image output**

In `Cdetect::detect_process`, wrap the existing result image call:

```cpp
    if (m_config_param.saveResult_img >= 1 || m_config_param.saveResult2txt >= 1)
    {
        perf::ScopedTimer timer("stage", "detect", "save_result_image", istate, static_cast<int>(vOutflaws.size()));
        save_result_img(param.img, param.jpgpath, param.jpgname, istate, vOutflaws);
    }
```

- [ ] **Step 7: Build and run existing tests**

Run:

```powershell
cmake --build --preset dev-relwithdebinfo-tests --target shell batch_summary_tests perf_profiler_tests
.\release\batch_summary_tests.exe
.\release\perf_profiler_tests.exe
```

Expected: both tests exit `0`, shell target builds.

- [ ] **Step 8: Commit Task 3**

```bash
git add proj2/detect.cpp proj2/detect.h
git commit -m "Measure detection pipeline stages" -m "Stage timers identify image read, main process, JSON output, and result-image output before optimizing inference code." -m "Constraint: Disabled profiling must keep the detection path cheap" -m "Confidence: medium" -m "Scope-risk: moderate" -m "Tested: batch_summary_tests and perf_profiler_tests"
```

---

### Task 4: Add Model Component Stage Timers And ROI Counts

**Files:**
- Modify: `proj2/area.cpp`
- Modify: `proj2/koujian.cpp`
- Modify: `proj2/element.cpp`

- [ ] **Step 1: Include profiler header in model component files**

Add to each file:

```cpp
#include "perf_profiler.h"
```

- [ ] **Step 2: Time area components**

In `Carea::process`, compute component name from the existing member `m_elementid`:

```cpp
    const std::string component = m_elementid.empty() ? "area" : m_elementid;
    int roi_count = 0;
```

After `batchsize_imgs(src, vimgs)` returns:

```cpp
    roi_count = static_cast<int>(vimgs.size());
```

Wrap the inference process body with:

```cpp
    perf::ScopedTimer timer("stage", component.c_str(), "process_total", -9999, -9999, roi_count);
```

Place the timer after the early `m_istate != 1 || src.empty()` return so disabled components do not emit rows.

- [ ] **Step 3: Time detail components**

In `Ckoujian::process`, use the same pattern:

```cpp
    const std::string component = m_elementid.empty() ? "detail" : m_elementid;
```

After ROI list construction:

```cpp
    const int roi_count = static_cast<int>(vimgs.size());
    perf::ScopedTimer timer("stage", component.c_str(), "process_total", -9999, -9999, roi_count);
```

Place the timer so it covers YOLO inference, filtering, and conversion for that detail component.

- [ ] **Step 4: Time element components**

In `Celement::process`, use:

```cpp
    const std::string component = m_elementid.empty() ? "element" : m_elementid;
```

After ROI list construction:

```cpp
    const int roi_count = static_cast<int>(vimgs.size());
    perf::ScopedTimer timer("stage", component.c_str(), "process_total", -9999, -9999, roi_count);
```

The timer should cover the component's model call and post-filtering, but not earlier disabled-state checks.

- [ ] **Step 5: Build**

Run:

```powershell
cmake --build --preset dev-relwithdebinfo-tests --target shell
```

Expected: shell and `proj2.dll` build successfully.

- [ ] **Step 6: Commit Task 4**

```bash
git add proj2/area.cpp proj2/koujian.cpp proj2/element.cpp
git commit -m "Measure enabled model component stages" -m "Component-level timers expose which area, detail, or element model dominates full-image processing and include ROI counts for cropped workloads." -m "Constraint: Disabled model sections should not emit synthetic timing rows" -m "Confidence: medium" -m "Scope-risk: moderate" -m "Tested: cmake build shell"
```

---

### Task 5: Enable Config-Only Stage Profiling For The Full-Model Run

**Files:**
- Modify: `config/project.xml`
- Modify: `config/config_guang3.xml`

- [ ] **Step 1: Add profiling node to project config**

In `config/project.xml`, under `<pthreading>`, add:

```xml
    <perf_profile enable="1" detail="stage" output_dir="perf"/>
```

Keep existing `auto_detect`, `thread_type`, and `thread_num` values unchanged.

- [ ] **Step 2: Enable all configured model sections**

In `config/config_guang3.xml`, set these model section states to `1`:

```xml
<area0 name="0_region1">
    <state state="1"/>
</area0>
<area1 name="1_region2">
    <state state="1"/>
</area1>
<detail0 name="2_detail1">
    <state state="1"/>
</detail0>
<detail1 name="3_detail2">
    <state state="1"/>
</detail1>
<element0 name="4_rail">
    <state state="1"/>
</element0>
<element1 name="5_dc_yiwu">
    <state state="1"/>
</element1>
<element2 name="2_dcqx">
    <state state="1"/>
</element2>
```

For any additional `elementN` sections present in the file, set `<state state="1"/>` for the profiling run.

- [ ] **Step 3: Build to stage config into release**

Run:

```powershell
cmake --build --preset dev-relwithdebinfo-tests --target shell
```

Expected: `release/config/project.xml` and `release/config/config_guang3.xml` match the source config after the build's runtime staging step.

- [ ] **Step 4: Commit Task 5**

```bash
git add config/project.xml config/config_guang3.xml
git commit -m "Enable full-model profiling configuration" -m "The profiling run turns on configured model sections and records stage-level timing through project.xml so deployment state is visible in configuration." -m "Constraint: Profiling is controlled by XML rather than environment variables" -m "Confidence: medium" -m "Scope-risk: narrow" -m "Tested: cmake build shell"
```

---

### Task 6: Verify Windows Stage Profiling End To End

**Files:**
- No source changes expected unless verification exposes a bug.

- [ ] **Step 1: Clear prior runtime profiling output**

Use PowerShell:

```powershell
if (Test-Path release\perf) { Remove-Item release\perf -Recurse -Force }
```

Expected: `release\perf` does not exist.

- [ ] **Step 2: Run the shell**

Run:

```powershell
.\release\shell.exe
```

Expected:

- console prints existing processing output
- console also prints `[perf] image=... total=...ms state=... flaws=...`
- `release\perf\perf_*.csv` exists

- [ ] **Step 3: Inspect CSV for required stage rows**

Run:

```powershell
Get-ChildItem release\perf\perf_*.csv | Select-Object -Last 1 | Get-Content | Select-Object -First 20
```

Expected header:

```text
run_id,timestamp,platform,image_path,thread_id,level,component,stage,duration_ms,state,flaws,roi_count,extra
```

Expected rows include:

```text
stage,shell,file_total
stage,detect,detect_process_total
stage,detect,main_process_total
stage,detect,read_image
```

Expected model component rows include at least one enabled `area`, `detail`, or `element` component from the active XML.

- [ ] **Step 4: Run unit tests again**

Run:

```powershell
.\release\batch_summary_tests.exe
.\release\perf_profiler_tests.exe
```

Expected: both exit `0`.

- [ ] **Step 5: Route any Windows verification failure back to the owning task**

If the Windows profiling run fails, do not patch from this verification task. Return to the specific owning task:

- CSV writer failure: return to Task 1
- missing shell `file_total`: return to Task 2
- missing `read_image`, `main_process_total`, `save_json`, or `save_result_image`: return to Task 3
- missing model component rows: return to Task 4
- missing config enablement: return to Task 5

After fixing the owning task, rerun Task 6 from Step 1.

---

### Task 7: Verify Jetson Build And Profiling Run

**Files:**
- No source changes expected unless Jetson build or runtime exposes a platform bug.

- [ ] **Step 1: Build on Jetson**

Run on Jetson:

```bash
cmake -S . -B build-jetson -DCMAKE_BUILD_TYPE=Release
cmake --build build-jetson -j
```

Expected:

- `build-jetson` configures successfully
- `shell_jetson` and `libproj2.so` build successfully

- [ ] **Step 2: Run on Jetson**

Run on Jetson from the runtime directory that contains `config/`:

```bash
./shell_jetson
```

Expected:

- existing detection output still appears
- `[perf]` console summaries appear
- `perf/perf_*.csv` is created

- [ ] **Step 3: Inspect Jetson CSV**

Run:

```bash
ls -lt perf/perf_*.csv | head
head -20 "$(ls -t perf/perf_*.csv | head -1)"
grep ',stage,.*process_total,' "$(ls -t perf/perf_*.csv | head -1)" | head
```

Expected:

- header matches Windows CSV header
- `platform` column is `jetson` on AGX Orin aarch64
- enabled model components emit `process_total`
- durations are positive for completed stages

- [ ] **Step 4: Route any Jetson verification failure back to the owning task**

If the Jetson profiling run fails, do not patch from this verification task. Return to the specific owning task:

- Linux build failure in profiler core: return to Task 1
- missing `dlsym` exports or shell `file_total`: return to Task 2
- missing detection-stage rows: return to Task 3
- missing component rows: return to Task 4
- missing config enablement: return to Task 5

After fixing the owning task, rerun Task 7 from Step 1.

---

### Task 8: Analyze Stage CSV And Decide The First Model-Level Hotspot

**Files:**
- No source or documentation changes in this task.

- [ ] **Step 1: Sort Jetson profiling rows by duration**

Run on Jetson or copy the CSV back to Windows and run an equivalent script:

```powershell
$csv = Get-ChildItem release\perf\perf_*.csv | Sort-Object LastWriteTime | Select-Object -Last 1
Import-Csv $csv.FullName | Sort-Object {[double]$_.duration_ms} -Descending | Select-Object -First 20 run_id,platform,image_path,component,stage,duration_ms,roi_count
```

Expected: top rows identify whether time is dominated by `save_result_image`, a model component such as `element1`, or a general detection stage.

- [ ] **Step 2: Choose the next owner from the sorted rows**

Use the sorted output to choose exactly one next lane:

- If the largest actionable row is `save_result_image`, write a new design for result-image output reduction before adding CUDA timers.
- If the largest actionable row is `read_image`, write a new design for image loading and input format handling before adding CUDA timers.
- If the largest actionable row is an `area`, `detail`, or `element` `process_total`, write a new design for model-level profiling of that component.
- If no row dominates, keep stage profiling enabled and collect a larger batch before changing inference internals.

Do not add model-level timers until this decision is made from CSV evidence.

---

## Self-Review Checklist

- Spec coverage:
  - config-only enablement: Task 1, Task 5
  - unified Windows/Jetson CSV schema: Task 1, Task 6, Task 7
  - shell `file_total`: Task 2
  - detection stages: Task 3
  - enabled model `process_total` and ROI counts: Task 4
  - all-model profiling config: Task 5
  - model-level detail deferred until hotspot evidence exists: Task 8
- Red-flag scan:
  - The plan contains no deferred fill-in markers.
  - Verification failures are routed back to concrete owning tasks instead of being patched through open-ended commands.
- Type consistency:
  - `perf::Detail`, `perf::ImageScope`, `perf::ScopedTimer`, `perf::record_event`, and `perf::record_file_total` are defined in Task 1 and reused consistently later.
  - Export names `configure_perf_profile` and `record_perf_file_total` are the names resolved by both shells.
