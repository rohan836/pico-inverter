"""Convert a Pico flash image to a UF2 file.

Normally ``pico_add_extra_outputs()`` uses picotool to create the UF2. If
picotool is not installed, this script produces the same RP2040 UF2 image.
It accepts either a flash ``.bin`` image or a linked firmware ``.elf`` file
(the latter is converted with ``arm-none-eabi-objcopy``).

Usage:
    python tools/bin2uf2.py build/pico_inverter.elf build/pico_inverter.uf2
    python tools/bin2uf2.py build/pico_inverter.bin  build/pico_inverter.uf2
"""

import shutil
import struct
import subprocess
import sys

FLASH_BASE = 0x10000000
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
UF2_FLAG_FAMILY_PRESENT = 0x00002000
UF2_FAMILY_RP2040 = 0xE48BFF56
PAYLOAD_SIZE = 256


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip())
        return 2

    in_path, out_path = sys.argv[1], sys.argv[2]
    if in_path.lower().endswith(".elf"):
        bin_path = in_path[:-4] + ".bin"
        objcopy = shutil.which("arm-none-eabi-objcopy")
        if not objcopy:
            print("error: arm-none-eabi-objcopy is not on PATH; pass a .bin file instead")
            return 1
        result = subprocess.run([objcopy, "-O", "binary", in_path, bin_path])
        if result.returncode != 0:
            print(f"error: objcopy failed with exit code {result.returncode}")
            return 1
        in_path = bin_path

    with open(in_path, "rb") as f:
        data = f.read()

    if not data:
        print("error: input image is empty")
        return 1

    blocks = (len(data) + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE
    with open(out_path, "wb") as out:
        for index in range(blocks):
            chunk = data[index * PAYLOAD_SIZE:(index + 1) * PAYLOAD_SIZE]
            chunk = chunk.ljust(PAYLOAD_SIZE, b"\x00")

            header = struct.pack(
                "<8I",
                UF2_MAGIC_START0,
                UF2_MAGIC_START1,
                UF2_FLAG_FAMILY_PRESENT,
                FLASH_BASE + index * PAYLOAD_SIZE,
                PAYLOAD_SIZE,
                index,
                blocks,
                UF2_FAMILY_RP2040,
            )
            out.write(header)
            out.write(chunk)
            out.write(b"\x00" * (476 - PAYLOAD_SIZE))
            out.write(struct.pack("<I", UF2_MAGIC_END))

    print(f"wrote {out_path}: {blocks} blocks, {len(data)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
