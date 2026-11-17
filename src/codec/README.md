# How Wiola reads audio and metadata

The codec layer gives the rest of Wiola a small, dependable view of an audio file. It answers three
questions: what the file contains, what we know about it without playing it, and how to turn it into
sound when playback begins. FFmpeg does the container and codec work, but its types and ownership
rules stay inside this directory.

This separation matters because reading tags and playing audio are different activities. Opening a
file for metadata must not move the playback position, and creating a second decoder must not disturb
the first one.

## Opening a file

[`LocalMediaProvider`](provider.cpp) begins by opening a local byte stream and returning a
[`media::MediaAsset`](../media/provider.hpp). An asset represents the resource, not a position in it.
It can inspect the file, read its metadata, or create independent decoders.

FFmpeg receives the file through [`media::ByteStream`](../media/byte_stream.hpp), rather than through
a filename passed to a global file API. [`FfmpegInput`](backends/ffmpeg/input.hpp) owns that stream
and the FFmpeg input built around it. This gives every inspection and decoder its own file cursor.
It also lets Wiola bound I/O, propagate cancellation, and reject attempts to open secondary
resources.

Recognition is based on content as well as the filename suffix. A file that looks like a supported
format is inspected for a usable audio stream, a known codec and profile, and a supported channel
layout. A familiar extension alone is not a promise that playback will work.

## Reading metadata without touching playback

[`read_metadata`](backends/ffmpeg/metadata.cpp) creates a new `FfmpegInput`, inspects the selected
audio stream, reads the fields Wiola uses, and then destroys that input. It never borrows the input
of a decoder that is already playing.

The process is:

1. Open an independent stream and let FFmpeg recognize the container.
2. Inspect the streams and select a supported audio stream, preferring the container's default when
   there is a choice.
3. Read `title`, `artist`, and `album` from the container tags. If a field is absent there, look in
   the selected audio stream's tags.
4. Ask the same inspection code used by playback for the stream's duration information.
5. Copy the first usable attached picture as encoded artwork.

Missing text remains empty rather than becoming a fabricated value. Text fields are limited to
64 KiB each. Artwork is limited to 8 MiB and remains in its original encoded form; PNG and JPEG are
identified explicitly, and no image decoding happens in the codec layer.

These rules live in [`metadata.cpp`](backends/ffmpeg/metadata.cpp). The tests in
[`ffmpeg_metadata_test.cpp`](../../tests/codec/ffmpeg_metadata_test.cpp) cover ordinary tags,
stream-level fallbacks, Unicode, artwork, and oversized input.

## Where the audio length comes from

Duration is not necessarily a tag written by a person. Unlike a title or an artist, it is usually a
fact reported or derived by the demuxer while it examines the container.

After stream inspection, [`FfmpegInput::inspect`](backends/ffmpeg/input.cpp) tries two sources, in
this order:

1. **The selected audio stream's duration.** This is the most specific answer. Its time base
   describes how to convert the stored duration units into seconds.
2. **The container's overall duration.** This is used when the selected stream has no duration of
   its own. Container duration uses FFmpeg's standard microsecond time base.

When either value is available and valid, Wiola converts it to seconds and then to a number of PCM
frames using the output sample rate. The metadata reader converts the same frame count back to the
shared time representation used by the rest of the application.

FFmpeg may obtain these values from container headers, sample tables, indexes, packet timestamps, or
other format-specific information. Depending on the demuxer, it may also estimate a container
duration from information such as bitrate and file size. Wiola does not implement a separate
file-size-divided-by-bitrate guess of its own.

If neither the stream nor the container supplies a usable non-negative duration, Wiola reports **no
known duration**. It does not scan the entire file merely to fill in the field.

## Does duration discovery decode the track?

**Wiola does not decode the whole track to determine its length.** The metadata path does not create
a [`PacketDecoder`](backends/ffmpeg/packet_decoder.hpp), enter the playback loop, or count decoded
frames through end-of-stream.

There is an important nuance: inspection is not necessarily a read-of-a-few-header-bytes operation.
Wiola calls FFmpeg's `avformat_find_stream_info`, and FFmpeg may read some packets or briefly invoke
a decoder to establish stream parameters. The input is configured with a 256 KiB probe size and a
one-second analysis-duration limit. Cancellation can interrupt this work through the input's
operation context.

So the precise guarantee is **no full-track decode by Wiola**, not “FFmpeg will never look at a
compressed audio frame.” For formats with useful indexes or duration fields, inspection can be very
cheap. For damaged or unusual files, FFmpeg may need more of the bounded probe before it succeeds,
fails, or leaves the duration unknown.

## Why a duration can be exact, estimated, or unknown

A displayed time and our confidence in that time are different things. The decoder therefore carries
a [`LengthAccuracy`](decode/decoder.hpp) alongside its optional frame count.

| Accuracy | What it means |
| --- | --- |
| `exact` | The reported frame count is allowed to bound playback. Wiola uses this for supported PCM and sample-count-based lossless formats, and for MP4 AAC when the stream itself supplies a duration. |
| `estimated` | A duration exists, but it must not cut off audible data. This covers formats whose container timing may omit codec delay, padding, or final frames. |
| `unknown` | No usable duration was found. Playback must rely on actual decoding progress and end-of-stream. |

This policy prevents a plausible-looking estimate from damaging the last seconds of a song. Exact
lengths can trim padding at the end of a read. Estimated lengths never truncate output, and a file
that ends before its estimate is not treated as damaged merely for doing so.

Unknown duration does not make a file unplayable. Playback continues until the decoder explicitly
reports end-of-stream or an error. Seek capability is reported separately, so “we do not know the
total time” does not automatically mean “we cannot seek.”

## What happens when playback actually starts

Creating a decoder is a separate operation from reading metadata. The asset asks the
[`MediaBackendRegistry`](decode/registry.hpp) for a new [`AudioDecoder`](decode/decoder.hpp), and the
FFmpeg backend builds fresh input, packet, and conversion state.

The resulting pipeline is easiest to understand as four responsibilities:

- [`FfmpegInput`](backends/ffmpeg/input.hpp) reads the container and selects the audio stream.
- [`PacketDecoder`](backends/ffmpeg/packet_decoder.hpp) turns compressed packets into decoded audio
  frames and reports when the source is exhausted.
- [`PcmConverter`](backends/ffmpeg/pcm_converter.hpp) converts those frames into the interleaved
  floating-point PCM shape promised to the engine.
- [`FfmpegDecoder`](backends/ffmpeg/decoder.hpp) coordinates their state and presents only neutral
  progress, end-of-stream, position, and error values to the caller.

A short read means that some audio was produced; it does not by itself mean the track ended. The
end-of-stream flag is explicit, and the final read may contain both audio and that flag. This lets
the engine play everything through the final drain rather than stopping at the container's packet
boundary.

## Threads: who does the work

The codec directory itself does not launch a general-purpose thread pool. Thread ownership belongs
to the engine and the audio device around it. This makes every potentially blocking operation
synchronous from the codec's point of view, while the caller decides which thread may block.

The following inventory is per [`Session`](../engine/session.hpp). Wiola's desktop application
currently has one session; additional sessions would have independent workers of their own. Qt and
the operating system may have internal threads that are outside this inventory.

### The application thread

The application thread is normally Qt's GUI thread. It handles user commands and calls
[`Session::catch_up`](../engine/session.cpp) to collect completed loads and metadata. The two
`QTimer` objects in [`MainWindow`](../gui/main_window.cpp) run on this same thread; timers do not
create worker threads. A non-GUI caller can use [`PlaybackService::pump`](../app/playback_service.cpp)
for the same job.

Opening a file returns before decoder inspection finishes. Later, `catch_up` installs the completed
decoder and applies completed tags to the playlist. This keeps FFmpeg I/O away from the GUI without
making the whole `Session` a lock-protected object that arbitrary threads may call.

### One loader worker

[`Loader`](../engine/load/loader.cpp) owns one `std::jthread`. It is created lazily by
[`Loader::start`](../engine/load/loader.cpp) on the first request and remains alive, usually sleeping
on a condition variable, until the loader is destroyed. There is one loader per session and at most
one decoder open physically in progress on it.

The call path is:

- [`Session::open`](../engine/session.cpp) and `Session::begin_reading` submit a request;
- [`Loader::start`](../engine/load/loader.cpp) stores the request and wakes the worker;
- [`Loader::work`](../engine/load/loader.cpp) calls the media provider and
  [`FfmpegDecoder::open`](backends/ffmpeg/decoder.cpp) synchronously;
- `Session::catch_up` collects the completion on the application thread.

Starting another request cooperatively cancels an active open, but there is never a second loader
thread doing a parallel open. A completed decoder is handed to playback and is not cancelled merely
because another load starts.

### One metadata worker

[`TagReader`](../engine/load/tag_reader.cpp) also owns one lazy, persistent `std::jthread`. Its entry
points are [`TagReader::start`](../engine/load/tag_reader.cpp), `TagReader::work`, and
`TagReader::take`. It reads one file's metadata at a time, even when the playlist contains hundreds
of files.

[`MetadataCoordinator`](../engine/load/metadata_coordinator.cpp) groups duplicate paths, submits
requests incrementally, and discards obsolete playlist generations. The reader has bounded pending
and completed queues, each holding up to 256 entries by default, plus a 64 MiB completed-artwork
budget. These bounds limit queued memory; they are not a pool of 256 workers.

The worker calls [`read_metadata`](backends/ffmpeg/metadata.cpp) synchronously. Metadata inspection
and decoder opening can therefore run in parallel with each other, but each has its own input and
file cursor.

### One active transport worker

Playback decoding gets a separate thread so that container reads, codec work, seeking, and PCM
conversion do not block either the GUI or the real-time audio callback.

[`Player::start`](../engine/transport/player.cpp) calls
[`TransportWorker::start`](../engine/transport/transport_worker.cpp), which launches one
`std::jthread` running `TransportWorker::work`. The worker calls
[`DecodeLoop::begin`](../engine/transport/decode_loop.cpp) and then
[`DecodeLoop::run`](../engine/transport/decode_loop.cpp). That loop invokes
[`FfmpegDecoder::decode`](backends/ffmpeg/decoder.cpp) synchronously and pushes PCM into the
playback queue.

A session has one active transport worker. It finishes after a stop, end-of-stream, or fault, and a
later replay joins any previous worker before starting another. Pause and resume change transport
state and wake this worker; they do not create replacement threads.

Seek preparation also runs here. The candidate decoder described in the next section is opened and
positioned on the transport worker, not on a new seek thread.

### The audio backend callback

[`Device`](../audio/device/device.cpp) does not explicitly create a `std::thread`. Its `start` method
configures a miniaudio data callback, and the platform audio backend invokes that callback on its
playback thread or callback stream. A session has one current output device and one serial callback
stream.

The callback runs [`Device::render`](../audio/device/device.cpp), which asks
[`BufferSource::render`](../audio/dsp/buffer_source.cpp) for already-decoded PCM. It does not read
compressed files or call FFmpeg.

The transport worker is the single producer for a [`PcmQueue`](../pcm/pcm_queue.hpp), and the audio
callback is its single consumer. This single-producer/single-consumer ring is the handoff between
the two threads. It avoids running decoder locks or file I/O inside the audio callback.

### No internal FFmpeg decoding pool

[`PacketDecoder::initialize`](backends/ffmpeg/packet_decoder.cpp) explicitly sets FFmpeg's codec
`thread_count` to 1. Decoding therefore uses the transport thread that made the call; Wiola does not
create an additional per-codec frame-thread pool. Inspection and metadata calls are likewise made
synchronously by their respective loader or metadata workers.

### How many run in parallel?

During ordinary playback, the active contexts are the application thread, the transport worker, and
the audio callback. The loader and metadata workers may exist but are asleep.

The busiest normal case can have **five active execution contexts per session**:

1. the application thread handling UI and completions;
2. the loader opening the next track;
3. the metadata worker inspecting another playlist entry;
4. the transport worker decoding the current track;
5. the audio callback consuming decoded PCM.

Seeking does not increase that count, and neither does creating a candidate decoder. When playback
is idle, only the application thread needs to be active; the loader and metadata workers can remain
alive but waiting until session destruction.

For a code-level walkthrough, follow `Session::open` into `Loader::work`, then follow
`Session::catch_up` and `Session::install_read_track` into `Player::start`. From there,
`TransportWorker::work` and `DecodeLoop::run` show the producer side, while `Device::render` and
`BufferSource::render` show the real-time consumer side.

## Seeking without losing the current position

A seek should not destroy a perfectly good playback position before we know that the new position
works. [`FfmpegDecoder`](backends/ffmpeg/decoder.cpp) therefore prepares an independent candidate
decoder, positions it, and decodes far enough to make the requested audible frame ready. Only a
successful candidate replaces the current input state.

Codec delay, timestamp offsets, and preroll stay inside the backend. The public contract is simpler:
either the requested position is ready, or the original decoder remains usable. Estimated and
unknown durations do not change that ownership rule.

## Errors, limits, and cancellation

Compressed files are untrusted input. Probe sizes, analysis time, metadata fields, artwork, decode
work, and seek work all have limits. Errors returned to the engine use
[`media::Result`](../media/result.hpp) and backend-neutral categories instead of leaking FFmpeg error
codes into application logic.

An [`OperationContext`](../media/operation_context.hpp) carries cooperative cancellation and an
optional deadline. Input callbacks and bounded work loops observe it, so replacing an active open
can stop unnecessary work. A completed decoder keeps its own operation state; starting another load
must not cancel audio that is already playing.

## A practical map for changes

For metadata or duration work, start here:

- [`provider.cpp`](provider.cpp) — connects a media asset to metadata and decoder creation.
- [`metadata.cpp`](backends/ffmpeg/metadata.cpp) — reads tags and artwork and publishes the optional
  duration.
- [`input.cpp`](backends/ffmpeg/input.cpp) — selects streams, finds stream/container durations, and
  classifies their accuracy.
- [`decoder.cpp`](backends/ffmpeg/decoder.cpp) — applies length policy during decoding and seeking.
- [`decoder.hpp`](decode/decoder.hpp) — defines the backend-neutral length and progress contracts.

For playback changes, continue with [`packet_decoder.cpp`](backends/ffmpeg/packet_decoder.cpp) and
[`pcm_converter.cpp`](backends/ffmpeg/pcm_converter.cpp). FFmpeg setup and the deliberately small
set of enabled demuxers and decoders are defined in
[`cmake/ffmpeg.cmake`](../../cmake/ffmpeg.cmake).

Useful checks are:

```sh
ctest --preset linux-debug -R 'FfmpegMetadata|FfmpegDecoder|FfmpegInput|FfmpegInspection'
ctest --preset linux-asan -R 'FfmpegHardening|FfmpegMetadata'
cmake --build --preset linux-headless -j
cmake --build --preset windows-release -j
```

Native Windows playback still needs to be tested on Windows. A successful MinGW cross-build proves
that the executable links, not that its audio path works at runtime.
