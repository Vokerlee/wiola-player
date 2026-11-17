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
