#!/bin/bash
#
# Build the payload volume: a small FAT image holding the EFI executable the
# firmware should start.
#
# This image is embedded in the firmware volume rather than written to a
# partition, because this board has no spare partition and nothing on the
# phone's storage should be destroyed to make one. The firmware publishes it
# as a RAM disk at boot, PartitionDxe and the FAT driver mount it, and the
# boot manager finds EFI/BOOT/BOOTAA64.EFI the same way it would on a real
# disk. Swapping in a different loader is then just replacing one file.
#
# Run from the uefi-uke root. Needs the pac-uefi-build image for mtools.
#
set -eu

TARGET="${1:-DEBUG}"
OUT=Resources/PacPayload/payload.img
SIZE_KB=360

BOOTAA64="Build/stallionPkg/${TARGET}_CLANGPDB/AARCH64/SiliciumPkg/Application/PacPayloadTest/PacPayloadTest/OUTPUT/PacPayloadTest.efi"

if [ ! -f "$BOOTAA64" ]; then
  echo "payload executable not built yet: $BOOTAA64" >&2
  echo "build the firmware once first, then re-run this" >&2
  exit 1
fi

docker run --rm -v "$PWD:/w" -w /w pac-uefi-build bash -c "
  set -eu
  rm -f '$OUT'
  # Small on purpose. This volume is carried inside the firmware image and
  # has to be read into a pool allocation during DXE, where free memory is
  # measured in a couple of megabytes. A 1.4 MB volume failed to load with
  # Out of Resources.
  mformat -C -f $SIZE_KB -v PACOS -i '$OUT' ::
  mmd -i '$OUT' ::/EFI
  mmd -i '$OUT' ::/EFI/BOOT
  mcopy -i '$OUT' '$BOOTAA64' ::/EFI/BOOT/BOOTAA64.EFI
  echo '--- payload volume contents ---'
  mdir -/ -i '$OUT' ::
"

echo "payload volume: $(wc -c < "$OUT") bytes at $OUT"
