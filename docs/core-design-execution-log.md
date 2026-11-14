# Core design execution log

## Step 01 — Behavioral baseline and dependency map

- **Baseline:** `b1bc5efd476d9bd49b8742549cbc1d68092069bb`.
- **Public contracts changed:** none; production and tests were unchanged. Behavior, thread
  ownership, output reuse, and dependency directions are recorded in `core-design-baseline.md`.
- **Validation:** Linux configure/build passed; 371/371 tests passed. Windows release configure
  and cross-build passed.
- **Unresolved risks:** no native Windows runtime, physical audio device, or interactive GUI test
  was performed. Suspected playback and concurrency defects still require focused reproducers.

## Step 02 — Deterministic engine test collaborators

- **Public contracts changed:** `Loader` and `TagReader` accept narrow open/read functions;
  `Session` can receive those collaborators alongside its output factory. Default construction
  still uses the concrete codec functions, and playback behavior is unchanged.
- **Tests added:** gated in-flight and obsolete loads, injected metadata reads, and controllable
  decoder read/seek/failure contracts. Manual `FakeOutput` remains the output-consumption seam.
- **Validation:** formatting checks passed; Linux 379/379 tests passed; ASan 379/379 passed;
  TSan 379/379 passed with each test repeated three times.
- **Unresolved risks:** injected third-party calls still cannot be physically interrupted during
  shutdown. Transport seek failures are observable by the new fakes but remain uncorrected until
  the planned seek-transaction step.
