#!/usr/bin/env python3
"""Wrap a BridgeCo .bcd firmware image in a minimal ARM ELF for disassembly.

The .bcd container has no magic a disassembler recognises, so a naive load
guesses the wrong processor and a base of zero -- every absolute reference in
the image then points nowhere.  The header carries the two facts that fix that:
the image's file offset and the address it is loaded at on the device.

Header field offsets are FFADO's, not inferred:
references/libffado-2.5.0/src/bebob/bebob_dl_bcd.cpp:193-235.

The payload is ARM little-endian (ARM946E-S / ARMv5TE -- the BridgeCo DM1000
core); the entry sequence at the image base is CP15 TCM setup.

Usage:
    python3 bcd_to_elf.py fw1814.bcd fw1814.elf
"""

import struct
import sys

# bebob_dl_bcd.cpp:213-231. Quadlets are little-endian.
OFF_IMAGE_OFFSET = 0x30
OFF_IMAGE_BASE = 0x34
OFF_IMAGE_LENGTH = 0x38
OFF_CNE_OFFSET = 0x50
OFF_CNE_LENGTH = 0x58

EM_ARM = 40
ET_EXEC = 2
PT_LOAD = 1
PF_RWX = 7
EHDR_SIZE = 52
PHDR_SIZE = 32
# Keep p_offset congruent to p_vaddr modulo p_align, as PT_LOAD requires.
DATA_OFFSET = 0x1000


def u32(blob: bytes, off: int) -> int:
    return struct.unpack_from("<I", blob, off)[0]


def build_elf(image: bytes, base: int) -> bytes:
    ehdr = struct.pack(
        "<16sHHIIIIIHHHHHH",
        b"\x7fELF\x01\x01\x01" + b"\x00" * 9,  # 32-bit, little-endian, SysV
        ET_EXEC,
        EM_ARM,
        1,  # e_version
        base,  # e_entry
        EHDR_SIZE,  # e_phoff
        0,  # e_shoff
        0x05000000,  # e_flags: EABI version 5
        EHDR_SIZE,
        PHDR_SIZE,
        1,  # e_phnum
        40,  # e_shentsize
        0,  # e_shnum
        0,  # e_shstrndx
    )
    phdr = struct.pack(
        "<IIIIIIII",
        PT_LOAD,
        DATA_OFFSET,
        base,  # p_vaddr
        base,  # p_paddr
        len(image),  # p_filesz
        len(image),  # p_memsz
        PF_RWX,
        DATA_OFFSET,  # p_align
    )
    pad = b"\x00" * (DATA_OFFSET - len(ehdr) - len(phdr))
    return ehdr + phdr + pad + image


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    with open(sys.argv[1], "rb") as f:
        blob = f.read()

    if blob[:4] not in (b"bCoD", b"BcOd"):
        print(f"not a BridgeCo container: magic={blob[:4]!r}", file=sys.stderr)
        return 1

    offset = u32(blob, OFF_IMAGE_OFFSET)
    base = u32(blob, OFF_IMAGE_BASE)
    length = u32(blob, OFF_IMAGE_LENGTH)
    cne_offset = u32(blob, OFF_CNE_OFFSET)
    cne_length = u32(blob, OFF_CNE_LENGTH)

    end = offset + length
    if end > len(blob):
        print(f"image runs past EOF: {end:#x} > {len(blob):#x}", file=sys.stderr)
        return 1
    # The CnE section is documented to follow the image; a mismatch means the
    # header was misread, so say so rather than emit a subtly wrong ELF.
    if cne_offset != end:
        print(
            f"warning: CnE at {cne_offset:#x} does not follow image end {end:#x}",
            file=sys.stderr,
        )

    with open(sys.argv[2], "wb") as f:
        f.write(build_elf(blob[offset:end], base))

    print(f"image   offset={offset:#x} base={base:#010x} length={length:#x}")
    print(f"CnE     offset={cne_offset:#x} length={cne_length:#x}")
    print(f"wrote   {sys.argv[2]}  (ARM LE, entry {base:#010x})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
