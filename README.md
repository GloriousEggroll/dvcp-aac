# Resolve AAC Bridge

AAC import/playback and export for DaVinci Resolve Studio on Linux x86-64.
The default build packages two plugins: the experimental decoder bridge and
the existing FDK-AAC export plugin with our ESDS descriptor correction.
Resolve extracts compressed packets; the plugin decodes them with statically
linked FDK-AAC. Guarded startup hooks connect the MP4/MOV and MKV readers to
Resolve's plugin decoder interface **in memory**. Application files remain unchanged.

This repository contains source only. Build output belongs in a separate
directory. No Resolve binaries, SDK files, user media, projects, diagnostic
recordings, or machine-specific installation state are included.

## Build

You need Python 3.8+, a working Podman or Docker Linux container engine, internet
access, and the codec SDK shipped with Resolve. No host compiler packages are
installed. On macOS/Windows the container engine needs its Linux VM; the result
is still a **Linux x86-64** plugin. Cross-host builds have not been tested.

```sh
python3 build.py --output ../aac-plugin-release
```

Use `--sdk /path/to/CodecPlugin/Examples/x264_encoder_plugin` for a different SDK
location, `--runtime docker` to choose Docker, or `--jobs 4` to limit compilation.
Both decoding and encoding are included by default. Use `--decode-only` if you
explicitly want to omit export support; `--with-encoder` is also accepted.

The builder fetches checksum-pinned FDK-AAC source, compiles in a digest-pinned
Rocky Linux 8 container, runs codec tests, checks ELF runtime requirements, and
produces a release directory plus `.tar.gz` and checksum. Temporary sources,
compiler packages, and the container are removed. A base image downloaded by
this build is removed too; pre-existing images are preserved. The release keeps
dependency source archives so binary recipients receive the corresponding source.

For an existing local toolchain, build FDK-AAC as a position-independent static
library, then run CMake with `-DRESOLVE_SDK=... -DFDK_ROOT=...`. This native route
uses the host's runtime baseline; prefer the container for redistribution.

## Install and test

Fully quit Resolve, then run these from the **built release directory**:

```sh
python3 install.py check --root /opt/resolve
python3 install.py install --root /opt/resolve
```

Relaunch Resolve. The installer preserves earlier copies and supports
`python3 install.py uninstall`. It never replaces Resolve or its FFmpeg libraries.
Do not combine this bridge with executable-patching AAC fixes.

`tools/check-resolve /opt/resolve/bin/resolve --test` additionally checks that
the scanner rejects changed instructions, duplicate signatures, and incorrect
RTTI, and can find synthetically relocated code. It does not modify the file.

## Compatibility and current limits

Validated locally with Studio **21.0.4.0005, Linux x86-64**, AAC-LC stereo at
48 kHz in MP4/MOV, and a missing-AudioSpecificConfig export. The ordinary SDK
decoder also worked in an MPEG-TS control. MKV AAC track discovery was checked
with 44.1 kHz mono and 48 kHz stereo test files; stereo PCM output was checked,
and a user confirmed import/playback of an actual MKV with the updated decoder.
MKV MP3, AC3 and FLAC control tracks still import through their native paths.

Studio **21.1.1.0010, Linux x86-64** is also recognized. That build moved the
QuickTime dispatch to different registers and added a field to
`IOPluginAudioCodec` (fields shifted by 0x18), so the scanner carries a second
signature layout and a matching entry stub. Validated there: scanner self-test,
bridge activation, and import/playback of AAC-LC stereo 48 kHz in MP4. The MKV
signatures are discovered on 21.1.1 but MKV playback was not tested on it.

For MKV, the bridge retains `A_AAC` tracks that Resolve otherwise drops, reads
the track's AudioSpecificConfig, and selects the plugin decoder. Resolve still
handles the container and video; compressed AAC packets are decoded on demand
during import, waveform generation and playback. No remux, transcode, sidecar
audio file or system FFmpeg process is used. This does not add new video codecs.
Malformed or unsupported AAC metadata leaves that track disabled.

The scanner discovers addresses from instruction patterns and verifies class
RTTI and the expected PCM implementation. Unknown or ambiguous layouts leave
the bridge inactive. This is an unofficial hook, not a promise of support for
other Resolve versions. Other AAC profiles and multichannel layouts need tests.

PCM is converted from the native plugin host's integer format to the float
format requested by the QuickTime and Matroska playback paths. Seek preroll is measured in
PCM samples, so two AAC frames request 2048 samples for the tested AAC-LC media.
When Resolve supplies a probe packet, the plugin can report FDK's measured
decoder delay. The tested QuickTime/Matroska paths supply no probe bytes.
Empty packets at end of stream drain FDK's remaining audio a bounded number of
times, then terminate the host read; returning success without output can make
Resolve spin forever while generating an MKV waveform. Sample-accurate delay,
tail trimming, multiple AAC tracks and other profiles still need validation.
Do not describe this as a
production-qualified replacement for Resolve's native codec support.

## Source layout

- `src/decoder.cpp`: codec registration, AAC configuration, decoding and PCM output.
- `src/bridge.cpp`, `bridge.S`, `bridge.h`: validated memory hook and PCM adapter.
- `src/aac_config.h`: bounded MKV AAC metadata parser.
- `src/diagnostics.h`: optional logging via `RESOLVE_AAC_LOG`.
- `tests/`: compatibility scanner and synthetic AAC codec/flush tests.
- `build.py`, `scripts/`: isolated, source-fetching build and artifact validation.
- `packaging/`: recipient instructions, installer and optional encoder correction.
- `sources.lock.json`, `licenses/`, `NOTICE.txt`: dependency provenance and notices.

The internal bundle name remains `aac_decoder_probe` so upgrades replace the
earlier experimental bundle without loading a duplicate codec registration.
The encoder bundle is `aac_fdk_plugin.dvcp.bundle`. It offers stereo AAC-LC
export in MP4/QuickTime at 44.1/48 kHz and 96–320 kbit/s. Both plugins contain
FDK-AAC and do not require the system's FFmpeg.

## Licensing

The original code in this project is licensed under the GNU General Public
License version 2 only (`GPL-2.0-only`). See [LICENSE](LICENSE) for the full text.

Third-party components retain their own licenses and attribution, as documented
in [NOTICE.txt](NOTICE.txt) and [licenses/](licenses/). Dependency source is
included in binary releases.
