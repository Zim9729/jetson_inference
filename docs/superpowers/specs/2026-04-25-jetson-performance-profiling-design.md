# Jetson Performance Profiling Design

Date: 2026-04-25
Project: `proj2_20260411`
Scope: add configurable performance profiling for Windows and Jetson runs so inference bottlenecks can be measured before optimization.

## Goal

The immediate goal is to find where Jetson inference time is spent before changing the inference implementation.

The profiling system should:

- run on both Windows and Jetson with the same CSV schema
- be enabled only from configuration files
- support a first pass that measures medium-grain pipeline stages
- support a second pass that measures model-internal hotspots only after the first pass identifies them
- work with all configured models enabled for the profiling run
- preserve current detection behavior when profiling is disabled

The primary question this design answers is:

> For each image, is time spent in file orchestration, image read, detection orchestration, a specific model, preprocessing, TensorRT enqueue, postprocessing, JSON output, or result-image output?

## Non-Goals

This design does not attempt to:

- optimize TensorRT engines yet
- change model accuracy, thresholds, or model input sizes
- introduce new dependencies
- redesign the detection pipeline
- replace OpenCV preprocessing
- add environment-variable overrides
- make profiling always on
- change checkpoint or auto-detect semantics

Optimization work should come after profiling data shows the real bottleneck.

## Current Project Context

The project already has Jetson support through:

- `libproj2.so`
- `shell_jetson`
- Jetson-native TensorRT, CUDA, and OpenCV discovery in CMake

The main detection stack includes:

- `shell/shell.cpp`
- `shell/shell_jetson.cpp`
- `proj2/DetAlgorithm.cpp`
- `proj2/detect.cpp`
- `proj2/area.cpp`
- `proj2/koujian.cpp`
- `proj2/element.cpp`
- `proj2/yolov5Trt.cpp`
- `proj2/tensorrt.cpp`

Existing configuration controls enabled models through `state` attributes in `config/config_guang3.xml`. For profiling this full pipeline, all model entries in the active configuration should be enabled so the CSV shows the real cost of the complete business workload.

## Configuration Design

Profiling is enabled only from configuration, not from environment variables or command-line overrides.

Add a profiling node under `config/project.xml` in the existing `<pthreading>` section:

```xml
<perf_profile enable="1" detail="stage" output_dir="perf"/>
```

Fields:

- `enable`
  - `0`: profiling is disabled
  - `1`: profiling is enabled
- `detail`
  - `stage`: medium-grain stage profiling
  - `model`: model-internal hotspot profiling
- `output_dir`
  - CSV output directory
  - relative paths are resolved from the runtime directory

If the node is missing, profiling is disabled.

Invalid values should degrade safely:

- invalid `enable`: treat as `0`
- invalid `detail`: treat as `stage` only when `enable="1"`, otherwise disabled
- empty `output_dir`: use `perf`

## Full-Model Profiling Requirement

The profiling run should use an active configuration where all configured model entries are enabled.

For `config/config_guang3.xml`, that means model sections such as `area0`, `area1`, `detail0`, `detail1`, `element0`, `element1`, and `element2` should use:

```xml
<state state="1"/>
```

This requirement belongs to the profiling scenario, not to the profiler module itself. The profiler records whatever the active configuration runs. The XML configuration remains responsible for deciding which models are enabled.

This separation allows later comparisons:

- full-model run
- single-model isolation run
- selected-model run

without changing profiler code.

## Profiling Module

Add a lightweight shared profiler module, for example:

- `proj2/perf_profiler.h`
- `proj2/perf_profiler.cpp`

The module should provide:

- configuration parsing state
- low-overhead disabled behavior
- RAII timers for scoped measurements
- thread-safe CSV writing
- concise console summaries

The design should avoid spreading hand-written start/end timing pairs throughout the code. A scoped timer makes the common path clearer:

```cpp
auto timer = perf::ScopedTimer(context, "stage", "element1", "process_total");
```

When profiling is disabled, scoped timers should be cheap and avoid file I/O.

## Stage Profiling

Stage profiling is the first pass and should be implemented before any model-internal profiling.

It should record medium-grain events that identify the slow component without heavily disturbing the runtime path.

Recommended instrumentation points:

- `shell/shell.cpp`
  - `file_total`
- `shell/shell_jetson.cpp`
  - `file_total`
- `proj2/DetAlgorithm.cpp::detect_process`
  - `detect_process_total`
- `proj2/detect.cpp::Cdetect::main_process`
  - `main_process_total`
- `proj2/detect.cpp::Cdetect::in_process`
  - `read_image`
  - `save_json`
  - `save_result_image`
- `proj2/area.cpp`
  - `area0.process_total`
  - `area1.process_total`
- `proj2/koujian.cpp`
  - `detail0.process_total`
  - `detail1.process_total`
- `proj2/element.cpp`
  - `element0.process_total`
  - `element1.process_total`
  - additional `elementN.process_total` entries as applicable

Where a stage processes cropped ROIs, the profiler should record `roi_count` when available. This is important because a model may be slow because it runs many times for one image rather than because one inference call is slow.

## Model Profiling

Model profiling is the second pass.

It should be added only for the model path identified as slow by stage profiling. The profiler may support both YOLOv5 and YOLOv10 instrumentation, but implementation should start with the actual hotspot.

YOLOv5 candidate stages in `proj2/yolov5Trt.cpp`:

- `yolov5_preprocess`
  - resize
  - padding
  - `blobFromImage`
  - host buffer copy
- `yolov5_h2d`
- `yolov5_enqueue`
- `yolov5_d2h`
- `yolov5_postprocess`
  - output traversal
  - confidence filtering
  - `NMSBoxes`
  - result conversion

YOLOv10 candidate stages in `proj2/tensorrt.cpp`:

- `yolov10_preprocess`
  - letterbox
  - `blobFromImage`
- `yolov10_h2d`
- `yolov10_enqueue`
- `yolov10_d2h`
- `yolov10_postprocess`

CUDA timing note:

`enqueueV2` and `cudaMemcpyAsync` can be asynchronous. Model profiling must define whether it measures CPU submit time or synchronized elapsed time. For bottleneck diagnosis, profiling should use synchronized timing when model-level detail is enabled. The non-profiling path should remain unchanged.

## Output Design

Profiling writes both console summaries and CSV.

### Console Summary

Console output should stay compact to avoid creating a new bottleneck.

Example:

```text
[perf] image=E1/test.jpg total=377ms read=12ms detect=341ms save_json=2ms save_img=18ms flaws=1
```

The console summary should report one line per processed image, not one line per measured event.

### CSV Output

CSV files are written under the configured output directory:

```text
perf/perf_YYYYMMDD_HHMMSS.csv
```

Use an event-style schema so dynamic model sets and later model-internal stages fit naturally.

Recommended fields:

```text
run_id,timestamp,platform,image_path,thread_id,level,component,stage,duration_ms,state,flaws,roi_count,extra
```

Field meanings:

- `run_id`: stable identifier for one process run
- `timestamp`: event timestamp
- `platform`: `windows`, `jetson`, or `linux`
- `image_path`: image being processed
- `thread_id`: logical detection thread id when available
- `level`: `stage` or `model`
- `component`: `shell`, `detect`, `area0`, `detail1`, `element1`, `yolov5`, `yolov10`, etc.
- `stage`: measured operation name
- `duration_ms`: elapsed milliseconds
- `state`: detection state when available
- `flaws`: flaw count when available
- `roi_count`: ROI count when available
- `extra`: optional details for future diagnostics

Example:

```text
20260425_182011,2026-04-25T18:20:11,jetson,E1/test.jpg,0,stage,detect,read_image,12,1,0,,
20260425_182011,2026-04-25T18:20:11,jetson,E1/test.jpg,0,stage,element1,process_total,312,1,1,4,
20260425_182011,2026-04-25T18:20:11,jetson,E1/test.jpg,0,model,yolov5,enqueue,24,1,1,,
```

## Threading And File Safety

The project already supports file-level concurrency through `thread_type` and `thread_num`.

The profiler should therefore:

- support concurrent calls from multiple worker threads
- serialize CSV writes with a small mutex
- avoid sharing mutable image-specific state globally
- include enough context in each event to reconstruct per-image timing

CSV write overhead is acceptable for profiling mode. When profiling is disabled, there should be no CSV lock activity.

## Windows And Jetson Comparison

The CSV schema is intentionally identical across platforms.

This allows direct comparison of:

- total image latency
- model-level time
- read and save cost
- CPU preprocessing cost
- postprocessing/NMS cost
- ROI count impact

The most useful comparison workflow is:

1. run the same image batch on Windows with profiling enabled
2. run the same image batch on Jetson with profiling enabled
3. sort CSV rows by `duration_ms`
4. compare slowest components by `component` and `stage`

Windows results should not be treated as Jetson performance, but they are useful for identifying code-path structure and detecting regressions introduced by instrumentation.

## Error Handling

Profiling should never break detection.

If profiling is enabled but CSV output cannot be opened:

- log a clear warning
- continue detection
- keep console summaries if possible

If an individual event is missing optional context:

- write an empty field
- do not fail the image

If config parsing fails:

- disable profiling
- continue current behavior

## Acceptance Criteria

### Profiling Disabled

With:

```xml
<perf_profile enable="0" detail="stage" output_dir="perf"/>
```

the program should:

- preserve current detection outputs
- avoid creating `perf_*.csv`
- avoid printing `[perf]` summaries
- avoid changing checkpoint, JSON, or result-image behavior

### Stage Profiling Enabled

With:

```xml
<perf_profile enable="1" detail="stage" output_dir="perf"/>
```

the program should:

- print one concise `[perf]` summary per processed image
- create a CSV file under the configured output directory
- include rows for `file_total`, `detect_process_total`, `main_process_total`, `read_image`, enabled model `process_total` stages, `save_json`, and `save_result_image`
- include rows for every model enabled by the active XML configuration
- keep missing or disabled models out of the CSV rather than fabricating zero-time rows

### Model Profiling Enabled

With:

```xml
<perf_profile enable="1" detail="model" output_dir="perf"/>
```

the program should:

- include all stage-level rows
- include model-level rows for the instrumented hotspot path
- report synchronized model timings when measuring CUDA work
- keep the non-profiling path unchanged

## Verification Plan

Windows verification:

```powershell
cmake --build --preset dev-relwithdebinfo-tests --target shell batch_summary_tests
.\release\shell.exe
```

Jetson verification:

```bash
cmake -S . -B build-jetson -DCMAKE_BUILD_TYPE=Release
cmake --build build-jetson -j
./build-jetson/shell_jetson
```

Manual CSV checks:

- file exists only when profiling is enabled
- header contains the expected fields
- rows include at least one processed image
- full-model config produces rows for all enabled model sections
- durations are positive for completed stages

## Risks

### Profiling overhead

CSV and synchronized CUDA timing can add overhead. This is acceptable in profiling mode, but disabled mode must stay cheap.

### Asynchronous CUDA measurement ambiguity

CUDA operations may appear artificially fast if only CPU submit time is measured. Model-level profiling should explicitly synchronize when detail is enabled.

### Full-model runtime cost

Turning on every configured model may make each image much slower. That is expected for full-pipeline diagnosis and should not be confused with normal production configuration.

### Result image output distortion

`saveResultImg=2` may make image output a meaningful part of runtime when defects are present. This is valuable to measure, but results should be interpreted separately from pure inference time.

## Recommended Execution Order

1. Add config parsing for `<perf_profile>`.
2. Add the shared profiler module with disabled-by-default behavior.
3. Add stage-level timers and CSV output.
4. Verify disabled behavior is unchanged.
5. Enable all configured models for the profiling run.
6. Run Windows stage profiling.
7. Run Jetson stage profiling.
8. Identify the slowest model path.
9. Add model-level detail only for that hotspot.
10. Use profiling data to choose the actual optimization work.
