# Batch Defects Summary Design

## Goal

When auto-detect processes a batch directory containing the four camera folders `E1`, `E2`, `E3`, and `E4`, generate one consolidated `defects.json` in the batch directory after inference for the four folders completes.

The existing per-image result JSON files remain unchanged. The new file is a batch-level summary for downstream consumption.

## Current Behavior

Auto-detect scans date-prefixed batch directories and collects supported image files from `E1`, `E2`, `E3`, and `E4`. Each image is processed through the existing detection path. When `saveResultJson` is enabled, each image writes a per-image result file under that image folder's `json` subdirectory:

```text
<batch>/E1/json/<image_stem>_result.json
```

## New Behavior

After `process_batch_directory()` finishes processing the collected files for a batch, the program scans:

```text
<batch>/E1/json
<batch>/E2/json
<batch>/E3/json
<batch>/E4/json
```

It reads each `*_result.json`, keeps only records whose `defects` array is non-empty, and writes a consolidated array to:

```text
<batch>/defects.json
```

The summary is regenerated from the per-image result files each time the batch finishes, so reruns and checkpoint resumes do not append duplicates.

## Output Shape

Each item in `defects.json` follows the sample file shape:

```json
{
  "count_fastening": 3,
  "defects": [],
  "imagePath": "20260415083000/E1/00018_-30405500_1776575709040.jpg",
  "mileage": "-30405.500",
  "mileageSign": "K"
}
```

`defects` is copied from the per-image result JSON.

`count_fastening` maps from the per-image `count_fastening` field if present. If the existing result uses `count_koujian` or only has the internal count value, the implementation should map the available count to `count_fastening`.

`imagePath` is relative to the parent of the batch directory, matching the sample style of `<batch>/<camera>/<image>`.

`mileage` is parsed from the second underscore-delimited filename field. For example:

```text
00018_-30405500_1776575709040.jpg
```

The value `-30405500` is millimeters and is converted to meters with three decimals:

```text
-30405500 mm -> "-30405.500"
```

`mileageSign` is `"K"`.

## Error Handling

Unreadable or malformed per-image JSON files are skipped with a log message. A bad filename mileage field does not block the whole batch; the item is skipped because the summary requires a valid mileage.

If no images have defects, write an empty JSON array:

```json
[]
```

## Implementation Boundary

Add the batch summary generation to the shell/auto-detect layer, after a batch directory has completed its normal per-image processing. Do not change model inference, defect filtering, or the existing per-image result JSON behavior.

## Verification

Use focused unit-style checks around the summary helper logic:

- Parse positive and negative millimeter mileage values from filenames.
- Generate `defects.json` from multiple camera folders.
- Exclude empty-defect results.
- Regenerate without duplicate entries.

Run the existing build or targeted compile checks after implementation.
