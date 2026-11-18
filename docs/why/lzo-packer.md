# Own LZO1X packer

## Decision

Voyemu packs the changed blocks of the QNX boot stream with its own LZO1X packer, written from the format description in the Linux kernel documentation (`Documentation/staging/lzo.rst`). The vendored lzokay decompressor (MIT) unpacks the stream. Both are compiled into Voyemu on every host; the GPL liblzo2 library is not used.

## Context

The QNX host image of the Free 6.0.3, 6.11.1 and 7.3.1 and Passion profiles is an LZO1X stream of 64 KiB blocks. [Preparation](../how/preparation.md#build) unpacks it, changes a few files and packs only the changed blocks again; unchanged blocks keep their OEM bytes. The OEM stream is as small as the LZO1X-999 packer of liblzo2 makes it. On Free 6.0.3 the startup accepts the stream only when block 104 starts within a ten-byte window, and the OEM stream starts it at the low end of that window, so the packed size of the changed blocks must match exactly.

## Alternatives

**liblzo2.** GPL-2.0-or-later, a separate library on every host and source-delivery obligations for every distribution that contains it.

**miniLZO.** The same license, and it only packs LZO1X-1, whose blocks are larger than the OEM ones.

**The lzokay packer.** MIT, but its blocks are larger than the OEM ones by up to 2 KiB each, so the stream of Free 6.0.3 cannot reach its window. Its structure follows the liblzo2 packer, so its independence from the GPL source is not established.

**GPL ports of the codec**, such as the Linux kernel `lib/lzo`. The license is the same as liblzo2.

## Argument

The packer finds the shortest encoding of a block over every match it offers within the format distances. On the OEM blocks of all published profiles its packing is about 1 % smaller than the best of liblzo2 LZO1X-999 levels 9 to 1. A changed block then fits in place of the OEM one. The same encoding can grow by an exact number of bytes: a copy of 3 to 8 bytes within 2 KiB has a two-byte form and a three-byte form. Growing the repacked blocks that way lands block 104 of Free 6.0.3 inside its window. Unpacking is a short routine that lzokay implements from the same kernel description, and preparation unpacks every stream it writes again and compares it with the intended plaintext.

## Consequences

`deps` installs no codec library, and Voyemu ships no GPL code for the boot stream. Packing one 64 KiB block takes about a tenth of a second, so a preparation that changes a few blocks spends under a second on it. When the changes for Free 6.0.3 enlarge the stream past the window, preparation repacks unchanged blocks as well, which takes about 15 seconds. lzokay reads a run of zero length bytes without an input bound, so Voyemu passes it a copy of each block followed by a non-zero byte. `src/licenses/README.md` records the origin of lzokay.
