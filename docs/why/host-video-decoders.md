# Host video decoders

## Decision

Preparation decodes the OEM startup videos with the H.264 decoder of the host operating system: VideoToolbox on macOS, the Media Foundation H.264 decoder on Windows and the system libavcodec on Linux. Voyemu neither builds, downloads nor ships a video codec. When a host has no decoder or the decoder rejects a video, that video becomes one black frame held for its length.

## Context

The OEM startup videos are H.264 in MP4, and `build` turns them into frames in the boot animation format for [the window](../how/window.md#startup-screen). An H.264 decoder in a distributed binary falls under the AVC patent license. Decoding runs once per machine during preparation, and a video that is not decoded only leaves its part of the startup screen black.

## Alternatives

**Build OpenH264 or FFmpeg from source into Voyemu.** The source licenses are free, but a published release would carry an H.264 decoder that needs its own patent license.

**Download Cisco's OpenH264 binary in `deps`.** Cisco covers the patent license only for binaries downloaded from Cisco, so every host depends on a server outside Voyemu's CI. That server refuses connections from some regions, where its pins can be neither taken nor verified.

## Argument

macOS and Windows license and update their own decoders, and on Linux the distribution supplies libavcodec; Voyemu calls them through thin `os/video_decoder_*` backends, following the [single implementation](single-implementation.md) rule. The MP4 reader, the level adjustment, the color conversion and the archive writer stay shared. A missing decoder costs only the startup videos, so preparation logs it and continues.

## Consequences

Windows N without the Media Feature Pack has no H.264 decoder and gets black startup videos. The OEM videos declare level 6.0 for 3840×1440 frames that fit level 5.1, which the Media Foundation decoder documents as its limit, so preparation lowers `level_idc` to the lowest level whose frame size, macroblock rate and reference frames the stream fits. Frames are converted with the matrix and range the stream declares. On Linux the backend loads the system libavcodec when one is installed, and a stand-in library replaces it in tests. [The window](../how/window.md#startup-screen) describes the startup screen.
