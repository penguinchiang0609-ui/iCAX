# External CAD recovery batch probe

Build `TubeDesignerTest` with the same configuration as the runtime DLLs first. Run from the repository with Python 3.9 or later:

```powershell
python src/tools/testing/run_imported_part_recovery.py --samples samples/structural-steel --runtime src/x64/Release --split-assemblies --output samples/structural-steel/recovery/verified
```

Use repeatable `--file relative/path.step` arguments for a subset. The default is one worker and a 300 second per-file timeout. `--workers` starts independent processes; values above one affect timing through CPU contention. The native probe is opt-in and reports `[not-run]` without exercising a sample during a normal test run. Only the runner's complete per-file reports establish sample results.

On timeout, Windows process-tree cleanup is bounded to 10 seconds. If the cleanup command fails, the runner terminates its own child using the retained process handle and waits at most another 10 seconds. A timeout remains `passed=false`; cleanup time is included in `processSeconds` and is not native recognition time.

The probe invokes the product's actual `ImportNestingPart`, `RecoverImportedPartDrawing`, `GetPartDrawing`, and `ApplyPartDrawing` methods. It checks that recovery leaves the original BRep bytes and resource reference intact. Successful saves must produce a valid BRep whose independently calculated bidirectional material difference is at most `1e-8` relative to the original. A rejected recovery is `unsupported` and `passed=false`, even when the native test confirms that rejection left the original unchanged.

The report records the original source hash and optional provenance from `manifest.json` (`samples[].file`). It does not claim a CAD is a real manufactured part merely because its test passes. Generated verification samples and supplier originals must be distinguished by the sample manifest.

Before importing, the probe counts the original STEP/IGES solids. Multi-solid input is reported as `assembly`, never passed as a single extrusion. With `--split-assemblies`, OCCT exports each original solid without geometric changes and the runner tests these files separately. Each child records `parentFile`, `parentSourceSha256`, and one-based `solidIndex`. Assembly totals and derived-solid totals remain separate in `summary.json`.

`summary.json` and `summary.csv` contain each phase's status, elapsed time, rejection reason and measured error. `importRecognizeFit` includes import, axis/contour recognition and fitting because the current product API executes them together; it does not pretend to measure these separately. `inspectSource` is the additional inventory pass. Native `totalSeconds` and wall-clock `processSeconds` are both reported. A partial report records the active phase before each call, so timeouts can be attributed without inventing stage timings.

The runner's exit code is nonzero for failed, timed-out or missing files. `unsupported` and `assembly` are explicit results and remain `passed=false`; callers should inspect the counts, not interpret exit code zero as support for every sample.
