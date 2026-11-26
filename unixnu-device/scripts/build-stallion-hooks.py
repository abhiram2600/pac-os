#!/usr/bin/env python3
"""Compile the board's own kernel hooks into an archive for the kernel link.

The kernel refers to a few symbols that no shared component defines and that
every board is expected to supply. They are compiled with the exact flags the
kernel itself was built with, taken from a compile record the kernel build
leaves behind, so the result cannot disagree with the kernel about its ABI.
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'kits/stallion/StallionBoardHooks.cpp'
OUT = ROOT / 'build/stallion-hooks'

record_path = (ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT'
               / 'IOPlatformExpert.cpo.json')
if not record_path.is_file():
    raise SystemExit(f'no kernel compile record yet: {record_path}')
record = json.loads(record_path.read_text().strip().rstrip(','))

OUT.mkdir(parents=True, exist_ok=True)
obj = OUT / 'StallionBoardHooks.o'

command, index, arguments = [], 0, record['arguments']
while index < len(arguments):
    value = arguments[index]
    if value == record['file']:
        command.append(str(SOURCE))
    elif value == '-o':
        command.extend(['-o', str(obj)])
        index += 1
    elif value == '-Xclang' and arguments[index + 1].startswith('-W'):
        index += 1
    elif not value.startswith('-W'):
        command.append(value)
    index += 1
command.extend(['-Wall', '-Wextra', '-Werror'])

result = subprocess.run(command, cwd=record['directory'], stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT, text=True)
(OUT / 'compile.log').write_text(result.stdout)
if result.returncode:
    raise SystemExit(result.stdout[-3000:])

archive = OUT / 'libStallionHooks.a'
archive.unlink(missing_ok=True)
subprocess.run(['xcrun', 'ar', 'rcs', str(archive), str(obj)], check=True)
print(f'stallion board hooks: {archive.name}')
