# Playback transport

This private owner keeps one decoder feeding one output. No codec, device SDK, Qt, DSP or application type enters this target. `Player` translates time commands, `TransportWorker` owns the thread, and `DecodeLoop` owns its complete execution and output cleanup. Buffering stays with the adjoining playback mechanism.

```mermaid
flowchart LR
    Commands[Player commands] --> Worker[TransportWorker]
    Worker --> State[Playback: first terminal reason wins]
    Worker --> Head[Playhead / SeekMailbox / epoch]
    Worker --> Loop[DecodeLoop: complete execution]
    Loop --> Plan[SeekPlan: owned decision values]
    Loop --> Seek[Immediate seek execution]
    Loop --> Media[AudioDecoder contract]
    Loop --> Queue[PCM producer]
    Loop --> Output[Output connection]
```

Start/restart and joining belong to one lifecycle owner. Pause/resume/stop/seek update synchronized state and wake the worker rather than performing decoder/device I/O. Requested playing state is distinct from observed output running state. The first terminal state remains settled until a new start; EOF or fault cannot replace an accepted stop.

The worker validates scheduling and chunk policy once at construction; decode storage separately checks conversion from frames to samples before allocating. The lifecycle caller alone begins a new execution. Once it has checked for an inactive state, pause/resume cannot make that state active so a second active-state refusal would be redundant. Restart joins the previous execution even when the caller did not explicitly wait for it.

Only the worker seeks, reads, primes or starts/stops output. `DecodeLoop::run` catches unexpected collaborator exceptions, settles `faulted`, and quiesces output on every exit. Already accepted terminal reasons remain authoritative. The worker joins before decoder/connection destruction; the connection and Player retain the output node, while the containing pipeline keeps queue storage alive until the worker has joined.

`Playhead` has one movable applier. Its mailbox serializes requests, claims and position/epoch observations; submitted frames are worker-owned. `SeekPlan` owns a claimed generation and target; its decisions need no device or decoder. DecodeLoop quiesces its output before passing that value to immediate `execute_seek`, which changes the decoder, acknowledges an achieved position and primes the new epoch. No seek object retains references to the loop’s internal fields. Failures retain the actual decoder position rather than publishing an unachieved request.

The target's build-only include root exposes `<transport/...>` to internal playback clients. These implementation headers are not installed. Application clients consume public playback values and the exported Session facade. Complete Session/loading/pipeline ownership stays in this component; private transport headers are not part of that export.

Seek startup and active requests use the same transaction. Its returned outcome distinguishes applied position, preserved cursor, read failure, cancellation and supersession; there is no mutable failure flag for a caller to remember to inspect. The decoder validates its own exact/approximate length rules. A no-request apply leaves output and queued audio alone.

A completed blocking seek acknowledges its actual decoder position, retiring only the claimed generation. Priming checks terminal state and newer requests before reading and again before submitting a completed block. Output stays quiesced until the decode loop has rechecked the mailbox; a superseded epoch is never restarted between requests. A stop before execution preserves the pending request without performing I/O. A stop during execution prevents further priming and output restart. Blocking collaborators still need to honor their own cancellation contract; this protocol cannot forcibly interrupt arbitrary backend calls.

Ordinary decoding also rechecks accepted stop/seek commands after a blocking read. That old epoch's PCM, error or EOF cannot override a pending command. In particular, consuming the last old block while requesting a seek must not let its delayed EOF end the track before the seek executes.

A preconstructed replacement may initially hold an inert output connection while the previous track owns the exclusive control. Before each worker launch, after joining its own prior execution, the setup thread retries that claim. The worker only uses the resulting control. Contention still accepts the start request and settles as eventual `faulted`; it never starts/stops the other owner's device.
