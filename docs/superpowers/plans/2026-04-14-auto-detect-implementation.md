# Auto Detect Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add configurable automatic detection for continuously arriving data directories by reading a top-level input path from `config/project.xml`, discovering today-prefix batch directories, scanning `E1` to `E4`, and polling repeatedly on both Windows and Jetson.

**Architecture:** Keep the change focused in the shell layer and the config schema. Add a small shared helper for date-prefix directory discovery and batch scanning so Windows and Jetson follow the same rules, while each shell keeps its own platform-specific startup and checkpoint handling. Reuse the existing file-level checkpoint mechanism instead of introducing filesystem watching or a new state store.

**Tech Stack:** C++17, `std::filesystem`, pugixml, existing Windows shell, existing Jetson shell, existing checkpoint helpers, CMake.

---

## File Structure Lock-In

**Create:**

- `e:/0_project/proj2_20260411/shell/auto_detect.h`

**Modify:**

- `e:/0_project/proj2_20260411/config/project.xml`
- `e:/0_project/proj2_20260411/shell/shell.cpp`
- `e:/0_project/proj2_20260411/shell/shell_jetson.cpp`

**Avoid modifying unless a build or runtime failure forces it:**

- `e:/0_project/proj2_20260411/proj2/detect.cpp`
- `e:/0_project/proj2_20260411/proj2/myxml.h`
- `e:/0_project/proj2_20260411/CMakeLists.txt`

## Execution Strategy

This plan keeps the implementation small and safe:

1. add the config switch and polling interval
2. add shared auto-detect helpers for today-prefix discovery and `E1` to `E4` scanning
3. wire the new polling loop into the Windows shell
4. wire the same logic into the Jetson shell
5. verify the behavior on both platforms with a real directory layout

### Task 1: Add Auto-Detect Settings To The XML Config

**Files:**

- Modify: `e:/0_project/proj2_20260411/config/project.xml`
- Test: manual XML parse check through existing startup code

- [ ] **Step 1: Extend the config shape**

Add an `auto_detect` node under `pthreading` with two attributes:

- `enable="1"` or `enable="0"`
- `poll_interval_ms="5000"` or another user-chosen interval

Keep the existing `path` node as the total directory.

- [ ] **Step 2: Preserve backward compatibility**

Make sure the XML stays readable if `auto_detect` is absent, so the current manual behavior still works for old configs.

- [ ] **Step 3: Save the config change**

Commit the updated XML after confirming the new attributes sit beside the current `path` setting.

### Task 2: Add Shared Helpers For Today-Prefix Discovery And Batch Scanning

**Files:**

- Create: `e:/0_project/proj2_20260411/shell/auto_detect.h`
- Test: compile-time include check from both shells

- [ ] **Step 1: Write the failing helper contract**

Define helper functions that both shells can call for the same behavior:

- build today’s `YYYYMMDD` prefix
- find all direct child directories whose names start with that prefix
- sort the matching directories by name ascending
- enumerate `E1`, `E2`, `E3`, and `E4` inside a batch directory

- [ ] **Step 2: Implement the minimal helper header**

Create a small header-only utility with `std::filesystem` helpers so both shells share the same discovery rules and do not drift apart.

Keep the helper narrowly focused on discovery and filtering. Do not move checkpoint logic into it.

- [ ] **Step 3: Verify the helper compiles in both translation units**

Make sure both `shell.cpp` and `shell_jetson.cpp` can include the header without any extra build-system changes.

- [ ] **Step 4: Commit**

```bash
git add shell/auto_detect.h
git commit -m "Add shared helpers for date-prefix auto detection"
```

### Task 3: Wire Auto-Detect Polling Into The Windows Shell

**Files:**

- Modify: `e:/0_project/proj2_20260411/shell/shell.cpp`
- Test: Windows shell still handles one-shot mode and new polling mode

- [ ] **Step 1: Write the failing behavior branch**

Extend the XML reader logic in `shell.cpp` so it loads:

- the total directory path from `path`
- the auto-detect enable flag
- the polling interval in milliseconds

At this stage, the shell should still have no polling loop yet, so the new config values are only read and stored.

- [ ] **Step 2: Implement the polling loop**

When auto-detect is enabled:

- resolve the total directory from `path`
- discover all today-prefix batch directories
- sort them by name ascending
- for each batch directory, scan only `E1` to `E4`
- reuse the existing checkpoint logic for every file path
- sleep for `poll_interval_ms` after each full scan cycle

Keep the existing one-shot path behavior available when auto-detect is disabled.

- [ ] **Step 3: Keep checkpoint writes immediate**

Preserve the current rule that each processed file updates the checkpoint immediately, so repeated polling does not reprocess completed files.

- [ ] **Step 4: Run a Windows smoke check**

Run the current Windows build and launch the shell against a test directory tree that contains:

- one total directory
- multiple same-day batch directories
- `E1` to `E4` subfolders
- a few new files added between polling cycles

Expected: the shell finds the batch directories in order, skips already processed files, and picks up new ones on the next cycle.

- [ ] **Step 5: Commit**

```bash
git add shell/shell.cpp config/project.xml shell/auto_detect.h
git commit -m "Add Windows auto-detect polling for dated batch directories"
```

### Task 4: Wire The Same Auto-Detect Flow Into The Jetson Shell

**Files:**

- Modify: `e:/0_project/proj2_20260411/shell/shell_jetson.cpp`
- Test: Jetson shell still loads the library and now supports the same polling rules

- [ ] **Step 1: Add the same config parsing branch**

Load the same auto-detect settings from `project.xml` in the Jetson shell so both platforms interpret the config the same way.

- [ ] **Step 2: Reuse the shared discovery helper**

Make the Jetson shell call the same today-prefix and `E1` to `E4` scanning helpers as Windows.

Keep platform-specific startup code unchanged except for the new polling branch.

- [ ] **Step 3: Preserve the Linux checkpoint path behavior**

Keep checkpoint generation and file-path comparison consistent with the current Jetson shell semantics so the new polling loop does not break resume behavior.

- [ ] **Step 4: Run a Jetson-oriented smoke check**

Use the existing Linux shell workflow to verify that:

- the library loads
- the config is read
- today-prefix batch directories are found
- multiple matching directories are processed in ascending name order
- new files appear in later polling cycles without reprocessing old ones

- [ ] **Step 5: Commit**

```bash
git add shell/shell_jetson.cpp shell/auto_detect.h config/project.xml
git commit -m "Add Jetson auto-detect polling for dated batch directories"
```

### Task 5: Validate Cross-Platform Behavior With A Real Directory Layout

**Files:**

- Test only: Windows shell runtime, Jetson shell runtime

- [ ] **Step 1: Prepare a representative directory tree**

Create a test layout that matches the expected production shape:

- total directory configured in `path`
- several same-day batch directories such as `20260411083000`, `20260411120000`, `20260411194226`
- each batch directory containing `E1` through `E4`
- a few image files in each `E*` folder

- [ ] **Step 2: Verify directory ordering and coverage**

Confirm that the shell processes all matching batch directories in ascending name order and ignores non-matching directories.

- [ ] **Step 3: Verify continuous polling**

Add a new image after the first polling cycle and verify that the next cycle detects it without reprocessing existing files.

- [ ] **Step 4: Verify checkpoint stability**

Restart the shell against the same test layout and confirm that already completed files are skipped.

- [ ] **Step 5: Capture the evidence and finalize**

Record the observed behavior for both Windows and Jetson so the feature can be checked against the acceptance criteria.

## Self-Review

### Scope coverage

- configurable auto-detect switch: covered in Task 1
- top-level directory from `path`: covered in Tasks 1, 3, and 4
- today-prefix batch directory discovery: covered in Task 2
- multiple matching directories in ascending order: covered in Tasks 2, 3, 4, and 5
- `E1` to `E4` only: covered in Tasks 2, 3, 4, and 5
- continuous polling: covered in Tasks 3, 4, and 5
- file-level checkpoint reuse: covered in Tasks 3, 4, and 5
- Windows and Jetson parity: covered in Tasks 3, 4, and 5

### Placeholder scan

- No `TODO`, `TBD`, or “similar to above” placeholders remain.
- Every task names exact files.
- The plan keeps the implementation small and local to the shell/config layer.

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-04-14-auto-detect-implementation.md`. Two execution options:**

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
