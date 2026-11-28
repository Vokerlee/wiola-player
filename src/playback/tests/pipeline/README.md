# Pipeline lifetime tests

These cases use the supplied output factory, existing FakeOutput/decoder and local ownership witnesses. They exercise output reuse, failed/null factory rollback, every preparation allocation failure, discarded candidates, attachment ownership across replacement, cache destruction and a pipeline that owns its output pin independently.

```mermaid
sequenceDiagram
    participant Test
    participant Cache as Output cache
    participant Pipeline
    participant Output
    Test->>Cache: prepare and activate lease
    Test->>Cache: replace or destroy cache
    Note over Pipeline,Output: pipeline lease keeps output alive
    Test->>Pipeline: destroy active pipeline
    Pipeline->>Output: stop and join callbacks
    Pipeline->>Pipeline: release decoder, then attachment and queue
```

The teardown witness composes the existing controlled device rather than implementing another callback simulator. An unbounded controlled decoder keeps playback active; the test observes actual device startup before destroying the pipeline. Decoder destruction must see a live, stopped output. The output may be released only afterward. Ordinary ASan/TSan exercise worker/callback teardown through the actual bundle.

The shared allocation witness runs only where the C++ allocator replacements are available; normal lifecycle cases remain enabled under TSan. No timing sleeps, device SDK or per-test manifest is needed. Use target `engine_pipeline_ownership_test` through ordinary CTest.

Session tests qualify the composition: null/invalid decoder completions report failure, output construction errors preserve active unseekable playback and authored processing, and every preparation allocation is failed in turn after unrelated metadata has settled. Output start failure is observed as eventual transport fault after installation. A GUI contract checks the unavailable-output status through the actual window.

`source_attachment_test.cpp` checks owned source/queue retention, cache reconstruction, wrong permissions/specs, in-flight callback retirement, and refusal to rebind while the previous controller owns the output. All synchronization uses real callbacks and explicit gates; no timing sleeps or private-field access are required.
