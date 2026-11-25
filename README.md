# PAC-OS

Running macOS on a Google Pixel 10a (codename `stallion`, Tensor G4).

The boot chain is `Google bootloader -> our UEFI -> UniXNU -> XNU -> macOS`.

## Layout

| Folder | What it is |
| --- | --- |
| `firmware/` | The UEFI firmware itself. This is what gets flashed to the phone. |
| `notes/` | What we have learned: verified register values, the device tree pulled off the phone, boot logs, and the state of each step. |

Start with `notes/FINDINGS.md`. It is the honest record of what works, what
does not, and which assumptions turned out to be wrong.

## Where the project is

Working and verified on the phone:

1. The bootloader loads our firmware.
2. The firmware brings up the SoC, the MMU and the timer.
3. **Display output.** The panel shows our own boot menu, not the Google logo.
4. UFS storage, at full speed, reading the partition table.
5. Loading and running a program off a filesystem.

Next is step 6: a real kernel loader in place of the placeholder program.

## Building

```
cd firmware
git submodule update --init --recursive
python3 build_uefi.py -d stallion -r DEBUG
```

The result is `firmware/Mu-stallion.img`, flashable with
`fastboot flash boot`.

## Credit

The firmware is built on Project Silicium's open UEFI work, which is in turn
built on Project Mu and EDK2. Those licences and copyright notices are kept
intact in the files they belong to. Everything specific to this phone, the
display bring-up, the storage bring-up, the memory map and the payload
volume, was written for this project.
