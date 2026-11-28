# Transport contracts

The focused executables cover Playback state arbitration, Playhead claims/epochs, TransportWorker lifecycle/failures, Player commands and seek transactions. They link the private transport target and controlled C++ collaborators, with no codec, filesystem, Qt or device SDK. `rig.hpp` belongs here because it only assembles the player, PCM queue and fake output used by these tests. Its small callback gate holds device progress until a test calls `play_out`; `hold_output` can hold a replay after the previous execution is joined. Intermediate state assertions do not race automatic exhaustion.

Player's ordinary source is generated PCM with an explicit stream shape and duration longer than the priming buffer. Tests that need unusual reads, unknown length, blocked seeks or failures supply their own decoder. Seek movement waits for the worker to apply its request and restart output before releasing device progress, then asserts the achieved request and consumed frame count. Stop checks quiescence and an undrained source instead of a wall-clock threshold; final position equals the controlled source duration. The real FFmpeg decoder-to-player journey stays in the codec integration suite instead of making transport unit tests prepare WAV files.

New worker failure cases use a gate to order stop/seek against a decoder failure. Existing broader player integration-style cases retain progress polling and are not claimed as deterministic scheduler proofs. Native TSan exercises actual threads; state/ownership tests assert outcomes independently of sanitizer diagnostics.

Typed decoder failures are checked separately from thrown exceptions: an ordinary read failure quiesces the running output, and failed seek priming never restarts it. Restart without an explicit wait exercises the worker's joining responsibility. Unseekable input leaves the reported position and decoder untouched. Duplicate playhead appliers cannot acknowledge another owner's seek, submit samples or report a drained epoch.

```mermaid
flowchart LR
    Cases[State / playhead / worker / player cases] --> Transport[Private transport]
    Transport --> Controlled[Controlled decoder and output]
    Integration[Codec integration suite] --> Real[Real decoder through Player]
```

Configure `WIOLA_COMPONENTS=playback` to run this owner with the portable media/audio/buffering closure alone. Public package consumers remain separate and install no transport implementation headers.

`engine_seek_operation_test` exhausts the value-only SeekPlan decision table (claim, replay, terminal state and interruption precedence) and checks initial/replay priming, no-op apply, preserved/approximate positions, exact-length rejection, read failure and EOF. Gates hold seek/read completion while another thread stops or replaces a request. Assertions check coherent position, outstanding request identity, no stale PCM submission and no unwanted output restart. No race case depends on a sleep or elapsed-time threshold.

Output handoff regressions construct the transport while a previous connection holds its claim, then release it before start. The launch collaborator observes that the claim was acquired on the setup thread before execution. A refused device start gives a bounded terminal result without sleeps. Contention leaves the previous device untouched and a subsequent start can retry after release. Both regressions fail before the handoff fix.
