# From an audio file to sound: a developer's guide

This guide follows one song through Wiola, from the moment it is opened until its samples reach the
audio device. It explains why the pieces exist and how they cooperate. The code links are optional
starting points, not a list of APIs to memorize.

FFmpeg reads containers and compressed audio. Miniaudio talks to the audio device. The codec layer
sits between them and turns format-specific details into a small set of ideas the rest of the player
understands: metadata, duration, PCM audio, playback position, and errors.

## 1. Start with a file, not a playback thread

When [`LocalMediaProvider`](provider.cpp) opens a path, it creates a
[`media::MediaAsset`](../media/provider.hpp). Think of the asset as “something Wiola can open,” not
as “something currently playing.” It has no playback cursor of its own.

That distinction gives us two useful freedoms:

- we can inspect a file without changing what is already playing;
- we can create more than one decoder from the same asset, and each decoder keeps its own position.

Every decoder receives an independent input. Metadata inspection, normal playback, and seek
preparation never have to share a file cursor.

## 2. Recognize the file before trusting it

A filename ending in `.mp3` or `.flac` is only a hint. Wiola also examines the file's contents, then
asks FFmpeg to identify the container and inspect its streams.

A usable file needs more than a recognized suffix. It must contain an audio stream whose codec,
profile, sample rate, and channel layout are supported. If several audio streams are present, the
container's default is preferred. A picture attached as album artwork must not accidentally become
the audio stream.

The input reaches FFmpeg through [`FfmpegInput`](backends/ffmpeg/input.hpp), which owns a
[`media::ByteStream`](../media/byte_stream.hpp). This gives Wiola control over reads, seeks,
cancellation, and file size. It also prevents FFmpeg from opening secondary resources behind our
back.

## 3. Read the tags without starting playback

Reading metadata is a separate visit to the file. The
[`read_metadata`](backends/ffmpeg/metadata.cpp) function opens its own input, inspects the selected
audio stream, collects the information, and closes that input again.

For title, artist, and album, Wiola first looks at the container's tags. If a field is missing
there, it tries the selected audio stream's tags. A missing field stays empty; Wiola does not invent
a replacement inside the codec layer.

Attached artwork is copied in its original encoded form. The codec does not decode the image. PNG
and JPEG are recognized explicitly, and oversized text or artwork is discarded rather than allowed
to consume unbounded memory.

Because this input is independent, reading tags cannot rewind or otherwise disturb a song that is
already playing.

## 4. Solve the duration puzzle

Duration is different from a title. It is often not a tag at all. Usually the demuxer—the part of
FFmpeg that understands the container—reports or derives it while inspecting the file.

Wiola tries two sources in order:

1. **The selected audio stream's duration**, because it describes the audio we actually selected.
2. **The container's overall duration**, when the stream has no duration of its own.

Both values need a time base. For example, a duration of `441000` with a time base of `1/44100`
means ten seconds. At a sample rate of 44,100 Hz, that becomes 441,000 PCM frames. The conversion
from FFmpeg's timing information to this neutral frame count happens in
[`FfmpegInput::inspect`](backends/ffmpeg/input.cpp).

Different containers provide different clues. Some have sample tables or indexes. Others leave
FFmpeg to use packet timestamps or estimate a duration from information such as bitrate and file
size. Wiola accepts FFmpeg's answer when it is usable; it does not add a second home-grown
file-size-divided-by-bitrate calculation.

If neither source produces a usable non-negative value, the duration is left **unknown**. Wiola does
not read the whole file merely to avoid showing an empty duration field.

### Does finding the duration decode the song?

**Not the whole song.** Wiola has no loop that decodes a track from beginning to end just to count
its frames. The metadata path does not even create the packet decoder used for playback.

There is a subtlety worth knowing: FFmpeg's stream inspection may read some packets, and for some
formats it may briefly invoke a decoder to establish stream facts. Wiola configures a 256 KiB probe
size and a one-second analysis-duration limit, and cancellation can interrupt the operation.

The honest summary is therefore “no full-track decode,” not “no compressed audio is ever touched.”

### Why some lengths are trusted more than others

A duration can look precise and still be a poor boundary for playback. Codec delay and padding are
classic examples: cutting output at an estimated container duration could remove real sound.

Wiola keeps three levels of confidence:

- **Exact** lengths may bound output. Supported PCM and sample-count-based lossless formats use
  this category, as does MP4 AAC when the stream itself supplies the duration.
- **Estimated** lengths are useful for display but never truncate audio.
- **Unknown** means there is no reliable total to display or use as a boundary.

Actual decoding progress and the explicit end-of-stream signal always decide when playback ends. An
unknown duration does not make a file unplayable, and it does not automatically make seeking
impossible.

## 5. Open a track without freezing the application

Now imagine selecting an album in the GUI. The application thread submits the work and returns to
handling the window. Two background workers can help with the album:

- **One loader worker** opens the track selected for playback. It performs container inspection and
  decoder construction away from the GUI. There is only one loader thread per session, so two
  decoder opens do not run in parallel on that worker.
- **One metadata worker** reads playlist tags one file at a time. Its queues can hold many requests,
  but those requests are not a pool of additional threads.

Both workers are created lazily on their first request and then reused. When idle, they wait rather
than repeatedly opening new threads. The application later collects their completions through
[`Session::catch_up`](../engine/session.cpp).

The loader path begins in [`Loader::start`](../engine/load/loader.cpp). The metadata path begins in
[`TagReader::start`](../engine/load/tag_reader.cpp). These are useful places to set a breakpoint if
you want to watch the work arrive.

## 6. Follow the audio after Play is pressed

A completed open gives the session a decoder. Pressing Play creates one transport worker for the
current playback run. This worker owns the blocking part of playback: reading the container,
decoding packets, converting samples, and preparing seeks.

The journey through the codec looks like this:

1. FFmpeg supplies a compressed packet.
2. The packet decoder turns it into a decoded audio frame.
3. The PCM converter turns that frame into interleaved floating-point samples.
4. The transport worker places those samples in a queue.

“PCM” simply means the uncompressed sample values an audio device can consume. “Interleaved” means
that channels alternate in the buffer: left, right, left, right for stereo.

The entry point for this work is [`DecodeLoop::run`](../engine/transport/decode_loop.cpp). FFmpeg's
codec is configured with one thread, so its decode call runs on this transport worker rather than
creating another codec-specific pool.

### Why the audio device needs a separate callback

The audio backend regularly asks for the next block of sound. That request arrives on miniaudio's
playback callback, owned by the platform audio backend rather than by an explicit `std::thread` in
Wiola.

This callback must not wait for file I/O or a slow decoder. It therefore does something much
simpler: it takes PCM that the transport worker has already produced. The
[`PcmQueue`](../pcm/pcm_queue.hpp) connects them. The transport worker is the only producer, and
the audio callback is the only consumer.

You can see the consumer side in [`Device::render`](../audio/device/device.cpp). There is no FFmpeg
call on that path.

## 7. Count the threads without guessing

For one session during ordinary playback, three execution contexts matter:

- the application thread handling UI and completions;
- the transport worker decoding audio;
- the audio callback consuming PCM.

The loader and metadata workers may exist, but they are asleep.

If the listener opens another track while the current one plays and playlist metadata is still
being read, five contexts can be active at once: the application thread, loader, metadata reader,
transport worker, and audio callback. That is the normal upper bound for one session—not a
per-file thread explosion.

Pause and resume reuse the transport worker. Stop lets it finish. Seeking also stays on that same
worker, so none of these actions adds another background thread.

## 8. Seek without throwing away a good position

A seek can fail for format-specific reasons: timestamps may be missing, a demuxer may reject the
request, or the target may lie beyond the actual audio. Wiola does not destroy the current playback
position before discovering those problems.

Instead, [`FfmpegDecoder`](backends/ffmpeg/decoder.cpp) prepares a candidate decoder on the
transport worker. The candidate is moved to the requested position and decoded far enough to make
the target audible frame ready. Only then does it replace the current input state. If preparation
fails, the original decoder remains usable.

The candidate is another decoder state, not another thread.

## 9. What to read first in the source

You do not need to read every class at once. This order follows the guide:

1. [`provider.cpp`](provider.cpp) shows how an asset provides metadata and new decoders.
2. [`metadata.cpp`](backends/ffmpeg/metadata.cpp) shows the independent tag-and-artwork visit.
3. [`input.cpp`](backends/ffmpeg/input.cpp) shows stream selection and the two duration sources.
4. [`decoder.cpp`](backends/ffmpeg/decoder.cpp) shows decoding, length policy, and transactional
   seeking.
5. [`loader.cpp`](../engine/load/loader.cpp) and
   [`transport_worker.cpp`](../engine/transport/transport_worker.cpp) show where the background
   work enters and leaves the codec.
6. [`device.cpp`](../audio/device/device.cpp) shows the final, FFmpeg-free audio callback.

By the end of that path, you have followed the file from bytes to metadata, from compressed packets
to PCM, and from the transport worker to the listener.
