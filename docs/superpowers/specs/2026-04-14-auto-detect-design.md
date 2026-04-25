# Auto Detect Design

Date: 2026-04-14
Project: `proj2_20260411`
Scope: add configurable automatic detection for continuously arriving data directories on both Windows and Jetson, using the existing file-level checkpoint mechanism.

## Goal

Enable the project to:

- configure a top-level input directory in `config/project.xml`
- optionally enable automatic detection from configuration
- find all batch directories for the current day by date-prefix match
- process every matching batch directory in chronological order
- scan only `E1`, `E2`, `E3`, and `E4` under each batch directory
- keep polling so newly arrived data can be detected later in the same day
- preserve the existing file-level checkpoint behavior
- work consistently in both `shell/shell.cpp` and `shell/shell_jetson.cpp`

The feature is considered successful when the application can point at a total data directory, discover all `YYYYMMDD*` batch directories for today, and keep detecting newly added images without reprocessing files that are already recorded in checkpoint files.

## Non-Goals

This design does not attempt to:

- redesign the detection algorithm itself
- add recursive scanning beyond `E1` to `E4`
- introduce file system event watching
- change the checkpoint file format
- support historical backfill across previous days automatically
- infer directory structure from naming beyond the date-prefix rule

## User Scenario

The intended data layout is:

- total directory configured in `project.xml`
- under that total directory, multiple batch directories named like `20260411194226`
- inside each batch directory, fixed subdirectories:
  - `E1`
  - `E2`
  - `E3`
  - `E4`
- actual image files live under those `E*` folders

Data may continue arriving during the day, so the program must not stop after a single scan. It should keep polling until the process is stopped.

## Configuration Design

Extend `config/project.xml` with an automatic detection block under the existing `pthreading` section.

Suggested shape:

```xml
<pthreading>
    <path path="E:\0_project\proj2_20260411\data"/>
    <auto_detect enable="1" poll_interval_ms="5000" run_date="20260411"/>
</pthreading>
```

### Fields

- `path`
  - points to the total input directory
  - example: `E:\0_project\proj2_20260411\data`
- `auto_detect.enable`
  - `1`: enable automatic detection
  - `0`: keep current manual / one-shot behavior
- `auto_detect.poll_interval_ms`
  - polling interval in milliseconds
  - user-configurable
- `auto_detect.run_date`
  - optional date prefix in `YYYYMMDD` format
  - when omitted, the application uses today’s date
  - when invalid, the application warns and falls back to today

### Compatibility rule

If `auto_detect` is missing, the application should preserve the existing behavior as closely as practical. That keeps the new feature optional and reduces risk for existing deployments.

## Directory Discovery Rules

When automatic detection is enabled:

1. Resolve the configured total directory from `path`.
2. Read `auto_detect.run_date` if present and valid; otherwise build today’s date prefix using local system date in `YYYYMMDD` format.
3. Enumerate direct child directories of the total directory.
4. Select every child directory whose name starts with the effective date prefix.
5. Sort the matching directories by name in ascending order.
6. Process each matching batch directory in order.
7. Repeat the scan on the next polling cycle so new data keeps getting picked up.

### Example

If today is `20260411`, and the total directory contains:

- `20260411083000`
- `20260411120000`
- `20260411194226`
- `archive`

then the program processes the three matching batch directories in this order:

1. `20260411083000`
2. `20260411120000`
3. `20260411194226`

This ordering matches the requirement that earlier directories are detected first, and the repeated polling ensures newly arriving directories on the same day are also discovered later.

## Batch Directory Scan Rules

For each matching batch directory:

- inspect only the first-level children named `E1`, `E2`, `E3`, and `E4`
- do not recurse into any deeper subdirectories
- process only supported image files under those folders
- keep the existing file-type selection logic already controlled by `imgtype`

This keeps the scan bounded and aligned with the actual data shape.

## Polling and Continuously Arriving Data

Automatic detection uses repeated polling rather than a one-time scan.

### Polling loop

- After each scan cycle, sleep for `poll_interval_ms`
- On the next cycle, rescan the total directory
- Newly created batch directories for the same day will be discovered on later cycles
- Newly added files inside previously discovered `E*` directories will also be found on later cycles

### Why polling instead of file watching

Polling is chosen because it is simpler, cross-platform, and consistent with the current shell architecture. It avoids introducing platform-specific file event watchers for the first iteration of this feature.

## Checkpoint Behavior

The current file-level checkpoint model remains the source of truth for progress tracking.

### Invariants to preserve

- checkpoint is stored beside the target directory using the existing naming convention
- a file is considered complete only by its full path
- after each file is processed, the checkpoint is written back immediately
- already completed files are skipped on subsequent scans

### Implication for continuous polling

Because the program may revisit the same directories many times during the day, checkpointing must remain path-based and stable. This allows the polling loop to safely re-scan the filesystem without duplicating work.

## Runtime Flow

### Startup

1. Load `project.xml`.
2. Read the total directory from `path`.
3. Read automatic detection settings.
4. Detect whether the feature is enabled.
5. If enabled, enter the polling loop.
6. If disabled, preserve existing one-shot behavior.

### Poll cycle

1. Find all batch directories for today under the total directory.
2. Sort them by name.
3. For each batch directory, scan `E1` to `E4`.
4. For each supported file, check the checkpoint.
5. Process unhandled files.
6. Update the checkpoint immediately after each file.

## Platform Strategy

The same logical behavior should exist on both platforms, but implementation details remain in the existing platform-specific shell files.

### Windows

- keep `shell/shell.cpp` as the Windows entry path
- add automatic detection there with minimal disturbance to existing behavior
- keep current dynamic-loading and execution style intact

### Jetson

- keep `shell/shell_jetson.cpp` as the Linux/Jetson entry path
- add the same automatic detection rules there
- preserve native Linux loading and filesystem behavior

### Shared behavior

The directory selection, sorting, and checkpoint semantics should be identical on both platforms so the feature behaves predictably regardless of runtime environment.

## Error Handling

The implementation should handle the following cases cleanly:

- total directory does not exist
- `project.xml` is missing or malformed
- `auto_detect` is enabled but `poll_interval_ms` is invalid or missing
- no batch directories match today’s prefix
- a batch directory exists but one of `E1` to `E4` is missing
- files appear or disappear during a polling cycle
- checkpoint read or write fails

### Required behavior

- log a clear message for each problem
- continue polling when the error is recoverable
- stop only when startup configuration is invalid or the user exits the process

## Testing Plan

### Configuration tests

- verify `project.xml` loads with `auto_detect` enabled
- verify missing `auto_detect` falls back to existing behavior
- verify custom polling interval is parsed correctly

### Directory discovery tests

- verify `YYYYMMDD*` matching returns all same-day batch directories
- verify directories are sorted by name
- verify non-matching directories are ignored

### Scan tests

- verify only `E1` to `E4` are scanned
- verify deeper nested folders are ignored
- verify files added later are picked up on the next polling round

### Checkpoint tests

- verify already processed files are skipped after rescan
- verify checkpoint updates immediately after each file
- verify restart resumes from previously completed files

### Platform parity tests

- verify Windows and Jetson share the same discovery rules
- verify both platforms process the same directory layout identically

## Risks

### 1. Duplicate processing if checkpoint paths are inconsistent

If file paths are normalized differently across platforms, the same file could be treated as different entries. The implementation should use stable full paths as early as possible.

### 2. Growing directory sets during polling

Because new files can keep arriving, the polling loop may keep discovering work indefinitely. That is expected, but it requires careful logging so operators can understand that the system is still active.

### 3. Configuration ambiguity

If `path` points to a batch directory instead of a total directory, the date-prefix search will behave differently. The implementation should document that `path` means the top-level total directory.

### 4. Long-running loop exit behavior

The polling loop must not block shutdown forever. It should rely on the process lifecycle and keep each cycle bounded.

## Acceptance Criteria

The feature is complete when:

1. `config/project.xml` can enable or disable auto detection.
2. The program reads a total directory from `path`.
3. The program finds all today-prefix batch directories under that total directory.
4. Matching directories are processed in ascending name order.
5. Each batch directory scans only `E1`, `E2`, `E3`, and `E4`.
6. The program keeps polling at the configured interval.
7. New files added later in the day are picked up automatically.
8. Already processed files remain skipped by checkpoint.
9. The behavior works on both Windows and Jetson.

## Why This Design

This design keeps the change focused on orchestration rather than inference logic.

It uses the existing checkpoint model, the existing shell entry points, and a simple date-prefix discovery rule so the feature can support continuously arriving data with low implementation risk.
