# Batch Defects Summary Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Generate one batch-level `defects.json` after auto-detect finishes processing `E1` through `E4`.

**Architecture:** Put the pure summary logic in a header-only helper under `shell/` so it can be shared by Windows `shell.cpp`, Jetson `shell_jetson.cpp`, and a focused test executable. The shell batch flow calls the helper after per-image processing completes.

**Tech Stack:** C++17, `std::filesystem`, `nlohmann::json`, CMake.

---

### Task 1: Add Batch Summary Tests

**Files:**
- Create: `tests/batch_summary_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/batch_summary_test.cpp` with tests that create a temporary batch directory containing `E1/json`, `E2/json`, `E3/json`, and `E4/json`. Write one defective result, one empty-defect result, and one malformed mileage filename result. Assert that `defects.json` contains only the valid defective item and that mileage `-30405500` becomes `"-30405.500"`.

- [ ] **Step 2: Add a CMake test target**

Add `batch_summary_tests` to `CMakeLists.txt`, include `shell`, `public`, and `${JSON_INCLUDE_DIR}`, and register it with `add_test`.

- [ ] **Step 3: Run test to verify it fails**

Run: `cmake --build build/windows-fresh --config Release --target batch_summary_tests`

Expected: build fails because `shell/batch_summary.h` does not exist.

### Task 2: Implement Summary Helper

**Files:**
- Create: `shell/batch_summary.h`

- [ ] **Step 1: Write minimal implementation**

Implement inline helpers:

```cpp
namespace batch_summary {
bool write_defects_summary(const std::filesystem::path& batch_dir);
}
```

The helper scans `E1` through `E4`, reads `*_result.json`, filters non-empty `defects`, parses mileage from the image filename's second underscore field as millimeters, formats meters with three decimals, writes `<batch>/defects.json`, and returns whether the file write succeeded.

- [ ] **Step 2: Run test to verify it passes**

Run: `cmake --build build/windows-fresh --config Release --target batch_summary_tests` and then `ctest --test-dir build/windows-fresh -C Release -R batch_summary_tests --output-on-failure`.

Expected: build succeeds and the focused test passes.

### Task 3: Wire Batch Flow

**Files:**
- Modify: `shell/shell.cpp`
- Modify: `shell/shell_jetson.cpp`

- [ ] **Step 1: Include the helper**

Add `#include "batch_summary.h"` beside `auto_detect.h`.

- [ ] **Step 2: Call after batch processing**

In each `process_batch_directory()`, after `process_directory_files(...)`, call:

```cpp
if (!batch_summary::write_defects_summary(batch_dir)) {
    std::cerr << "[batch summary write failed] " << (batch_dir / "defects.json").string() << std::endl;
}
```

- [ ] **Step 3: Build shell targets**

Run: `cmake --build build/windows-fresh --config Release --target shell batch_summary_tests`.

Expected: both targets build.

### Task 4: Final Verification

**Files:**
- No new files.

- [ ] **Step 1: Run focused tests**

Run: `ctest --test-dir build/windows-fresh -C Release -R batch_summary_tests --output-on-failure`.

Expected: PASS.

- [ ] **Step 2: Inspect git diff**

Run: `git diff --stat` and review changed files for accidental unrelated edits.

Expected: only plan, helper, test, CMake, and shell wiring files are changed.
