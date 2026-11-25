# Local file access

LocalFileStream implements the public media ByteStream hooks with an owned native `ifstream`. Link `wiola::local_media`; format identification, tags and decoding belong to its consumers, not this adapter.

```mermaid
flowchart LR
    Consumer[Provider or decoder input] --> Port[Public ByteStream admission]
    Port --> File[LocalFileStream hooks]
    File --> Handle[Owned ifstream]
```

The static open operation checks context and regular-file access before returning ownership. Construction is private, so callers receive a successfully opened object. Public byte operations perform common cancellation/deadline and count admission; the file hooks own native ranges, stream flags, cursor movement and I/O failures. Destruction closes the handle after its owning consumer releases it.

Seek captures the current native stream position after clearing any prior EOF state. If movement is rejected, it clears the failure and restores that position before reporting `seek_failed`; the next read therefore sees the original bytes or a usable EOF. If native restoration itself fails, the resource reports `unreadable` and must be abandoned rather than claiming an intact cursor. Offset conversion uses the standard checked integer-range operation.

Real byte-stream integration cases live in [tests](tests); portable byte-contract cases live in [media tests](../tests). LocalFileStream has no Qt, device or codec SDK dependency. Its checkout include is `<media/local/file_stream.hpp>`.

`provider.hpp` and `provider.cpp` compose local-file access with the codec registry through MediaProvider/MediaAsset. The separate `wiola_codec_provider` target owns that format-aware composition; the local source/stream target remains independent of it. Provider integration cases live with [codec integration tests](../codecs/tests).

[resource.hpp](resource.hpp) and [resource.cpp](resource.cpp) own conversion between native paths and provider references. `local_resource` records generic UTF-8 path spelling and the stem as a fallback label, without opening or canonicalizing a file. `local_resources` preserves order and duplicate occurrences. `local_path` verifies the local namespace before conversion and rejects embedded nulls instead of allowing a native open to truncate a key. Different provider namespaces cannot gain local access by supplying a key that resembles an existing filename. LocalMediaProvider checks cancellation/deadlines before conversion or opening.

[file_replacement.hpp](file_replacement.hpp) and its implementation own recoverable native media-file replacement. Link `wiola::file_replacement` independently of streaming or codecs. The port accepts explicit source/staging/backup paths and supplied digests; the native implementation verifies artifacts and records its recovery journal before destructive steps. Typed results distinguish I/O failure, conflict, cancellation and required recovery. The owning application decides when playback is quiescent and when a successful replacement may update catalog metadata.
