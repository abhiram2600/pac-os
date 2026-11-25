#!/usr/bin/env python3
"""Pre-flight check for the stallion memory map.

MemoryPeim in SiliciumPkg sorts the memory map and calls CpuDeadLoop() if any
two DDR-side regions overlap, which on this device looks exactly like a silent
boot failure. This reproduces that check on the host, plus confirms every
DDR-side region actually lands inside one of the Pixel 10a's four RAM banks
(its 8 GB is NOT contiguous).

Run from the uefi-uke root:  python3 Resources/PacDebug/validate_memory_map.py
"""
import re
import sys

MAP = "Platforms/Google/stallionPkg/Library/MemoryMapLib/MemoryMapLib.c"

# gArmTokenSpaceGuid.PcdSystemMemoryBase; regions below this are register
# regions and MemoryPeim skips them in the overlap check.
SYSMEM_BASE = 0x80000000

# memory@... nodes from the zumapro device tree.
RAM_BANKS = [
    (0x80000000,  0x80000000),
    (0x880000000, 0x80000000),
    (0x900000000, 0x80000000),
    (0x980000000, 0x80000000),
]


def main():
    rows = re.findall(r'\{"([^"]+)",\s*(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)',
                      open(MAP).read())
    regions = sorted(((n, int(a, 16), int(l, 16)) for n, a, l in rows),
                     key=lambda r: r[1])
    print(f"{len(regions)} regions in {MAP}\n")

    problems = 0

    for i in range(len(regions) - 1):
        name, addr, length = regions[i]
        next_name, next_addr, _ = regions[i + 1]
        if addr < SYSMEM_BASE:
            continue
        if addr + length > next_addr:
            print(f"OVERLAP: {name} (0x{addr:X}..0x{addr + length:X}) "
                  f"runs into {next_name} (0x{next_addr:X})")
            problems += 1

    for name, addr, length in regions:
        if addr < SYSMEM_BASE:
            continue
        if not any(addr >= b and addr + length <= b + s for b, s in RAM_BANKS):
            print(f"NOT IN RAM: {name} (0x{addr:X}..0x{addr + length:X}) "
                  f"is outside every RAM bank")
            problems += 1

    if problems:
        print(f"\n{problems} problem(s) found - MemoryPeim would dead-loop "
              f"or map non-existent memory")
        return 1

    print("clean: no overlaps, every DDR-side region inside a real RAM bank")
    return 0


if __name__ == "__main__":
    sys.exit(main())
