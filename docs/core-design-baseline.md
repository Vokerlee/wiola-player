# Core behavioral baseline and dependency map

Baseline: `b1bc5efd476d9bd49b8742549cbc1d68092069bb`, reviewed on 2026-11-14.
These rules describe behavior that later steps must preserve unless a change names a defect and
its intended correction. Source references are to that baseline.

## Commands and queue behavior

- **B01 — Open:** `Session::open` replaces the queue, selects its first entry, starts metadata
  reads, and asynchronously opens that entry. A nonempty open reports `loading`; existing playback
  continues until `catch_up` installs a successful result. The newly installed track is idle. A
  failed open leaves the previously loaded track and its playback state intact and reports the
  failure through `open_result` (`session.cpp`, `session_test.cpp`).
- **B02 — Add:** `Session::add` appends without replacing the loaded track or changing transport
  state. If the queue was empty and no track is loaded, it opens the first added track but does
  not start playback. Metadata is requested for every added path.
- **B03 — Selection versus loaded track:** the playlist cursor chooses which resource a navigation
  command will open; it is not the identity of the currently loaded decoder. `Session::track`
  names the loaded resource, not merely the selected queue row.
- **B04 — Navigation and explicit selection:** `next_track`, `previous_track`, and `play_track`
  return false without opening anything when the requested queue position does not exist. A valid
  command opens the selection and preserves whether playback was active: playing selections play
  after installation; idle, paused, stopped, or ended selections are installed idle.
- **B05 — Remove:** removing a queue row does not unload or interrupt the loaded track, including
  when that row names the loaded resource. Playlist removal itself selects the following row, or
  the last remaining row when necessary, without opening it.
- **B06 — Clear:** clearing empties the queue and stops playback but retains the loaded pipeline.
  The retained track can be played again without reopening its file.
- **B07 — Transport toggle:** with no loaded track, `play_or_pause` returns false. While playing it
  pauses; while paused it resumes; from idle or a terminal state it starts. Starting a previously
  finished or stopped player rewinds it unless a pending seek chooses another start position.
- **B08 — Stop:** stop discards buffered playback, enters `stopped`, and requests position zero.
  It does not unload the track. Stop and device failure do not trigger automatic queue advancement.
- **B09 — End of track:** `ended` means decoded audio has drained through the output. `catch_up`
  then advances according to the playlist's repeat/shuffle order and plays the next track. At the
  end of the order, the ended track remains loaded.
- **B10 — Seek:** a requested position is published immediately and applied by the decoding
  thread. Seeking while paused remains paused; seeking while stopped or ended is held until the
  next start. The public contract says positions beyond the end are ignored. Validation and failed
  backend seeks are later defect work, not a new baseline feature.
- **B11 — Settings:** volume and equalizer objects belong to the session and survive track and
  stream-spec changes. GUI settings adapters persist their values; codec and transport code do
  not own GUI settings keys.
- **B12 — Metadata:** queued rows initially use their file stem. Metadata completion updates every
  duplicate row with the same path, preserving each row's path and queue position. Empty title
  metadata does not replace that initial title.

## Transport states

`Playback::State` is the public transport state. With no pipeline, `Session` reports `idle`.

| From | Command or event | To | Required result |
| --- | --- | --- | --- |
| `idle`, `ended`, `stopped`, `faulted` | successful start | `playing` | previous worker is joined before reuse |
| `playing` | pause | `paused` | output is stopped by the decode worker |
| `paused` | resume | `playing` | output is restarted by the decode worker |
| any nonterminal state | stop | `stopped` | first terminal result wins |
| playing/draining | all submitted audio consumed | `ended` | queue may advance on `catch_up` |
| active playback | output fails or stops unexpectedly | `faulted` | no automatic queue advance |

A second start while playing or paused is rejected. Pause/resume calls from the wrong state are
rejected. Once `ended`, `stopped`, or `faulted` wins a race, later terminal requests do not replace
it. These rules are covered by `playback_test.cpp`, `player_test.cpp`, and `session_test.cpp`.

## Output and pipeline lifetime

- **B13 — Output reuse:** a session constructs one output for the first loaded stream spec and
  reuses it for later tracks with an equal spec. A different spec replaces the output. Replay of
  one loaded track reuses both output and decoder.
- **B14 — Installation order:** installing a decoder first points the relay at silence, destroys
  the old player and buffer, configures the persistent DSP chain, and creates or reuses the
  output. It then constructs the new buffer/player and points the relay at the new buffer source.
- **B15 — Ownership:** a pipeline owns its decoder, ring buffer, buffer source, and player. Player
  destruction stops and joins its decode worker before decoder destruction. Session member order
  keeps the DSP stages alive while the chain refers to them and keeps output/source storage alive
  while a player refers to them.

## Thread ownership

- The GUI thread owns `Session`, `Playlist`, and settings calls and drives completion through
  `Session::catch_up`; current window refresh is therefore also application progress.
- `Loader` has one lazy `jthread`. Its caller submits/collects under a mutex; its worker performs
  concrete `codec::open_file` without holding that mutex. Latest-request-wins drops obsolete
  completions. Destruction requests stop and joins.
- `TagReader` has one lazy `jthread` and FIFO work/completion queues protected by a mutex. Its
  worker performs concrete `codec::read_tags`; destruction requests stop and joins.
- `Playback` atomics accept transport requests from the GUI thread and terminal transitions from
  the decode worker. The first terminal transition wins.
- At baseline, `Player::start` joins any old worker and calls `DecodeLoop::begin` on its caller.
  `begin` seeks, primes the ring, and starts output before the new worker is launched. After that,
  only the decode worker drives output start/stop and decoder read/seek operations.
- `Playhead` permits seek requests and position reads from other threads. Its single claimed
  applier belongs to the decode worker, which alone claims seeks and changes the pushed count.
- The output callback consumes the lock-free buffer through `BufferSource`; it does not call the
  codec decoder. `Output::frames_played` is monotonic and is not reset by tracks or seeks.

## Public dependency directions

Current CMake targets already provide these boundaries:

```text
main -> cli + gui -> engine session -> engine load/queue/transport
                              |             |          |
                              v             v          v
                         audio DSP       codec      audio device + codec decode
                              |                                  |
                              +-------------- PCM ---------------+
```

More specifically:

- queue depends only on queue value types and the standard library;
- load depends on the codec aggregate for opening and tags;
- transport depends on audio device contracts and codec decode, not DSP or Qt;
- session is the composition point for queue, load, transport, codec, audio DSP, and output;
- DSP and codec share PCM/units/core foundations; engine headers do not include Qt, TagLib,
  miniaudio, or dr_libs headers;
- `main.cpp` selects the concrete GUI application, while the default `Session` constructor selects
  the miniaudio-backed `Device`. Tests inject `Output` through `OutputFactory`.

The present broad `src` include root and unconditional Qt discovery are build-boundary debts for
later steps; this baseline does not treat them as permission to add reverse dependencies.

## Baseline verification

Run from the baseline commit on Linux:

```bash
cmake --preset linux
cmake --build --preset linux -j
ctest --preset linux
cmake --preset windows-release
cmake --build --preset windows-release -j
```

Results on 2026-11-14:

- Linux configure and `RelWithDebInfo` build succeeded; **371/371 tests passed** in 4.55 seconds.
- Windows `Release` configure and MinGW-w64 cross-build succeeded, producing
  `build-win-release/bin/wiola-player.exe`.
- The Windows executable was not run on native Windows. No Windows runtime, physical audio
  device, or interactive GUI result is claimed.

Suspected defects in the improvement plan remain hypotheses until focused reproducers establish
them. In particular, step 03 must demonstrate incorrect absolute output-counter accounting before
replacing it with epoch-relative accounting.
