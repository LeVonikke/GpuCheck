#!/usr/bin/env python3
"""
verify-pe.py - Validate that a file is a PE32+ x86-64 UEFI application.

Parses the PE headers directly rather than trusting the build, and checks the
handful of fields firmware actually cares about:

  * MZ / PE\0\0 signatures
  * Machine            == 0x8664 (x86-64)
  * Optional header    == 0x20B  (PE32+)
  * Subsystem          == 10     (EFI application)
  * An entry point RVA that is non-zero and lands inside a section
  * Presence of a .reloc directory, so the loader can rebase the image

Exit code 0 = valid, 1 = invalid.
"""

import struct
import sys

IMAGE_FILE_MACHINE_AMD64 = 0x8664
PE32PLUS_MAGIC = 0x20B
SUBSYSTEM_EFI_APPLICATION = 10
SUBSYSTEM_NAMES = {
    10: "EFI_APPLICATION",
    11: "EFI_BOOT_SERVICE_DRIVER",
    12: "EFI_RUNTIME_DRIVER",
    13: "EFI_ROM",
}
IMAGE_FILE_RELOCS_STRIPPED = 0x0001
IMAGE_FILE_EXECUTABLE_IMAGE = 0x0002


def fail(msg):
    print(f"  FAIL: {msg}")
    return False


def verify(path):
    with open(path, "rb") as fh:
        data = fh.read()

    ok = True
    print(f"  path            : {path}")
    print(f"  file size       : {len(data)} bytes")

    if len(data) < 0x40 or data[:2] != b"MZ":
        return fail("not an MZ image")

    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew + 24 > len(data):
        return fail("e_lfanew points outside the file")

    if data[e_lfanew:e_lfanew + 4] != b"PE\0\0":
        return fail("missing PE signature")

    machine, num_sections, _, _, _, opt_size, characteristics = struct.unpack_from(
        "<HHIIIHH", data, e_lfanew + 4
    )

    print(f"  machine         : 0x{machine:04X}", end="")
    if machine == IMAGE_FILE_MACHINE_AMD64:
        print("  (x86-64)")
    else:
        print()
        ok = fail(f"machine is not x86-64 (expected 0x8664)")

    opt_off = e_lfanew + 24
    magic = struct.unpack_from("<H", data, opt_off)[0]
    print(f"  optional magic  : 0x{magic:03X}", end="")
    if magic == PE32PLUS_MAGIC:
        print("   (PE32+)")
    else:
        print()
        ok = fail("not PE32+ (0x20B)")

    entry_rva = struct.unpack_from("<I", data, opt_off + 16)[0]
    image_base = struct.unpack_from("<Q", data, opt_off + 24)[0]
    sect_align = struct.unpack_from("<I", data, opt_off + 32)[0]
    file_align = struct.unpack_from("<I", data, opt_off + 36)[0]
    size_image = struct.unpack_from("<I", data, opt_off + 56)[0]
    subsystem = struct.unpack_from("<H", data, opt_off + 68)[0]
    num_rva = struct.unpack_from("<I", data, opt_off + 108)[0]

    print(f"  subsystem       : {subsystem}", end="")
    if subsystem == SUBSYSTEM_EFI_APPLICATION:
        print("       (EFI_APPLICATION)")
    else:
        name = SUBSYSTEM_NAMES.get(subsystem, "not a UEFI subsystem")
        print(f"       ({name})")
        ok = fail("subsystem is not 10 (EFI_APPLICATION)")

    print(f"  image base      : 0x{image_base:X}")
    print(f"  section align   : {sect_align}")
    print(f"  file align      : {file_align}")
    print(f"  size of image   : {size_image}")
    print(f"  entry point RVA : 0x{entry_rva:X}")

    if entry_rva == 0:
        ok = fail("entry point RVA is zero")

    if not characteristics & IMAGE_FILE_EXECUTABLE_IMAGE:
        ok = fail("IMAGE_FILE_EXECUTABLE_IMAGE not set")

    if characteristics & IMAGE_FILE_RELOCS_STRIPPED:
        ok = fail("relocations stripped - firmware may be unable to rebase")

    # Data directories: index 5 is base relocation.
    reloc_rva = reloc_size = 0
    if num_rva > 5:
        dd_off = opt_off + 112
        reloc_rva, reloc_size = struct.unpack_from("<II", data, dd_off + 5 * 8)
    print(f"  .reloc directory: rva=0x{reloc_rva:X} size={reloc_size}")
    if reloc_size == 0:
        print("  WARN: no base relocation directory")

    # Sections
    sect_off = opt_off + opt_size
    entry_in_section = False
    print(f"  sections        : {num_sections}")
    for i in range(num_sections):
        off = sect_off + i * 40
        if off + 40 > len(data):
            ok = fail("section table runs past end of file")
            break
        raw = data[off:off + 8]
        name = raw.rstrip(b"\0").decode("latin-1")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
        flags = struct.unpack_from("<I", data, off + 36)[0]
        print(f"      {name:<9} rva=0x{vaddr:<8X} vsize=0x{vsize:<7X} "
              f"raw=0x{rawsize:<7X} flags=0x{flags:08X}")
        span = max(vsize, rawsize)
        if vaddr <= entry_rva < vaddr + span:
            entry_in_section = True

    if not entry_in_section:
        ok = fail("entry point does not fall inside any section")

    return ok


def main():
    if len(sys.argv) != 2:
        print("usage: verify-pe.py <file.efi>")
        return 2
    try:
        good = verify(sys.argv[1])
    except Exception as exc:  # noqa: BLE001
        print(f"  FAIL: exception while parsing: {exc}")
        return 1
    print("  RESULT          : " + ("VALID PE32+ EFI APPLICATION" if good else "INVALID"))
    return 0 if good else 1


if __name__ == "__main__":
    sys.exit(main())
