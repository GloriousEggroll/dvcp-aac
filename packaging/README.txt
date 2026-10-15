Resolve AAC Bridge — Linux x86-64, experimental

This package enables AAC import/playback through a decoder plugin and a guarded
in-memory hook. It does not edit Resolve's executable or bundled codec libraries
on disk. FDK-AAC is compiled into the plugin; system FFmpeg is not required.

Supports AAC import/playback in MP4, MOV and MKV. Resolve's existing video codec
support still applies; the plugin does not add video decoders or MKV export.
MKV AAC is decoded on demand, with no remuxing or intermediate audio files.

Tested: DaVinci Resolve Studio 21.0.4.0005 on Linux x86-64. MP4/MOV AAC-LC stereo,
including missing AudioSpecificConfig; MKV mono/stereo track discovery, stereo
PCM output and user-confirmed import/playback. The hook checks the
host code before activating. Passing its checks does not establish support for
every Resolve version. Other AAC profiles, channel layouts, multiple AAC tracks
and sample-accurate delay/tail trimming need testing. End-of-stream decoding is
bounded to prevent a waveform-generation loop on partial final AAC frames.
Also tested: Studio 21.1.1.0010, MP4 AAC-LC stereo 48 kHz import/playback; MKV
is recognized there but untested.

Install (Python 3.8 or newer):
  1. Save your project and fully quit Resolve.
  2. Run: python3 install.py check --root /opt/resolve
  3. Run: python3 install.py install --root /opt/resolve
     Use sudo for step 3 only if the Resolve directory is not writable.
  4. Start Resolve again, import your clip, and check playback.

The default package includes both plugins:
  aac_decoder_probe.dvcp.bundle — AAC import/playback.
  aac_fdk_plugin.dvcp.bundle — AAC export (stereo AAC-LC, MP4/QuickTime,
                              44.1/48 kHz, 96–320 kbit/s).
The installer installs both. A custom --decode-only build omits the encoder;
BUILD.json explicitly lists which features are present in the package.

Alternatively, copy the complete .dvcp.bundle folder(s) into Resolve's IOPlugins
folder while Resolve is closed. Preserve the bundle and binary filenames. Do
not leave two copies of the same decoder plugin installed under different names.
The installer is preferred: it checks compatibility and preserves previous copies.
If Resolve was patched by another AAC patcher, restore its stock files first.

Undo the last scripted install:
  python3 install.py uninstall --root /opt/resolve
Previous plugins are retained in .aac-plugin-backups under your Resolve folder.
Manual copies are removed manually while Resolve is closed.

Updates must also be installed with Resolve fully closed. Restarting clears old
decoder state and in-memory audio buffers. Routine waveform-cache deletion is
not required.

Diagnostics: Resolve's own log records whether the bridge activated. Optional
detailed logging is enabled by starting Resolve with RESOLVE_AAC_LOG set to a
writable filename. It is disabled by default and contains no hardcoded user path.

The binaries are built against GLIBC 2.28 / GLIBCXX 3.4.25 or older. They still
need the usual Linux C/C++ runtime libraries; see BUILD.json for exact requirements.
SHA256SUMS records package integrity. check-resolve reads but never edits Resolve.

Share the complete release archive, including licenses and third-party-source.
Those source archives provide the exact dependency source used for these binaries.
The separate aac-plugin-source directory contains the bridge source and builder.
