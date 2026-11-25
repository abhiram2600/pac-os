/**
  Publishes the embedded payload volume as a block device.

  This board has no spare partition, and nothing on the phone's storage
  should be destroyed to make one, so the payload filesystem travels inside
  the firmware image as a raw file. This driver finds it, copies it into
  memory, and installs a block device over it.

  From there nothing is special: PartitionDxe and the FAT driver attach to
  it, and the boot manager discovers EFI/BOOT/BOOTAA64.EFI exactly as it
  would on a real disk. Replacing the loader means replacing one file in
  Resources/PacPayload.

  Block I/O is implemented here rather than by using MdeModulePkg's
  RamDiskDxe, which carries an HII configuration page and an ACPI NVDIMM
  table and asserts out of its file explorer on a platform this minimal. A
  RAM-backed disk is about a hundred lines, so the dependency is not worth
  it.

  Copyright (c) 2026, PAC-OS.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DiskIo.h>
#include <Protocol/FirmwareVolume2.h>
#include <Protocol/SimpleFileSystem.h>

#define PAC_PAYLOAD_BLOCK_SIZE  512

//
// The raw file in the firmware volume, matching the FILE FREEFORM GUID in
// stallion.fdf.
//
STATIC CONST EFI_GUID  mPacPayloadFileGuid = {
  0x7A3F1D62, 0x9C84, 0x4E05, { 0xB1, 0xD7, 0x2F, 0x6A, 0x08, 0xC3, 0xE5, 0x19 }
};

typedef struct {
  EFI_BLOCK_IO_PROTOCOL    BlockIo;
  EFI_BLOCK_IO_MEDIA       Media;
  UINT8                   *Buffer;
  UINTN                    Size;
} PAC_PAYLOAD_DISK;

STATIC PAC_PAYLOAD_DISK  mDisk;
STATIC EFI_HANDLE        mDiskHandle = NULL;
STATIC UINTN             mReadCount  = 0;

//
// A vendor-defined device path, so PartitionDxe has something to hang its
// own path off and the boot manager can name what it booted from.
//
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} PAC_PAYLOAD_DEVICE_PATH;

STATIC PAC_PAYLOAD_DEVICE_PATH  mDevicePath = {
  {
    { HARDWARE_DEVICE_PATH, HW_VENDOR_DP, { sizeof (VENDOR_DEVICE_PATH), 0 } },
    { 0x7A3F1D62, 0x9C84, 0x4E05, { 0xB1, 0xD7, 0x2F, 0x6A, 0x08, 0xC3, 0xE5, 0x19 } }
  },
  { END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, { END_DEVICE_PATH_LENGTH, 0 } }
};

STATIC
EFI_STATUS
EFIAPI
PacDiskReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                 ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
PacDiskReadBlocks (
  IN  EFI_BLOCK_IO_PROTOCOL  *This,
  IN  UINT32                  MediaId,
  IN  EFI_LBA                 Lba,
  IN  UINTN                   BufferSize,
  OUT VOID                   *Buffer
  )
{
  PAC_PAYLOAD_DISK  *Disk = (PAC_PAYLOAD_DISK *)This;
  UINTN              Offset;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (MediaId != Disk->Media.MediaId) {
    return EFI_MEDIA_CHANGED;
  }

  if ((BufferSize % PAC_PAYLOAD_BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Offset = (UINTN)Lba * PAC_PAYLOAD_BLOCK_SIZE;
  if (Offset > Disk->Size || (Offset + BufferSize) > Disk->Size) {
    DEBUG ((
      EFI_D_WARN,
      "payload: read past end, lba %lu size %u\n",
      Lba, BufferSize
      ));
    return EFI_INVALID_PARAMETER;
  }

  mReadCount++;

  CopyMem (Buffer, Disk->Buffer + Offset, BufferSize);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
PacDiskWriteBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                  MediaId,
  IN EFI_LBA                 Lba,
  IN UINTN                   BufferSize,
  IN VOID                   *Buffer
  )
{
  PAC_PAYLOAD_DISK  *Disk = (PAC_PAYLOAD_DISK *)This;
  UINTN              Offset;

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (MediaId != Disk->Media.MediaId) {
    return EFI_MEDIA_CHANGED;
  }

  if ((BufferSize % PAC_PAYLOAD_BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Offset = (UINTN)Lba * PAC_PAYLOAD_BLOCK_SIZE;
  if (Offset > Disk->Size || (Offset + BufferSize) > Disk->Size) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Disk->Buffer + Offset, Buffer, BufferSize);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
PacDiskFlushBlocks (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

/**
  Pull the payload image out of whichever firmware volume carries it.
**/
STATIC
EFI_STATUS
PacReadPayloadFromFv (
  OUT VOID   **Buffer,
  OUT UINTN   *Size
  )
{
  EFI_STATUS                      Status;
  EFI_HANDLE                     *Handles;
  UINTN                           HandleCount;
  UINTN                           Index;
  EFI_FIRMWARE_VOLUME2_PROTOCOL  *Fv;
  UINT32                          AuthStatus;

  Handles     = NULL;
  HandleCount = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiFirmwareVolume2ProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "payload: no firmware volumes! %r\n", Status));
    return Status;
  }

  DEBUG ((EFI_D_WARN, "payload: searching %u firmware volume(s)\n", HandleCount));

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiFirmwareVolume2ProtocolGuid,
                    (VOID **)&Fv
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    *Buffer = NULL;
    *Size   = 0;

    Status = Fv->ReadSection (
                   Fv,
                   &mPacPayloadFileGuid,
                   EFI_SECTION_RAW,
                   0,
                   Buffer,
                   Size,
                   &AuthStatus
                   );
    if (!EFI_ERROR (Status)) {
      DEBUG ((EFI_D_WARN, "payload: found %u bytes in firmware volume %u\n", *Size, Index));
      FreePool (Handles);
      return EFI_SUCCESS;
    }

    DEBUG ((EFI_D_WARN, "payload: fv %u ReadSection RAW: %r\n", Index, Status));

    //
    // Fall back to reading the whole file. A FREEFORM file's payload is
    // reachable either way, and which one works depends on how the section
    // was encapsulated.
    //
    {
      EFI_FV_FILETYPE    FileType;
      EFI_FV_FILE_ATTRIBUTES  Attributes;

      *Buffer = NULL;
      *Size   = 0;

      Status = Fv->ReadFile (
                     Fv,
                     &mPacPayloadFileGuid,
                     Buffer,
                     Size,
                     &FileType,
                     &Attributes,
                     &AuthStatus
                     );
      if (!EFI_ERROR (Status)) {
        DEBUG ((
          EFI_D_WARN,
          "payload: read %u bytes as a whole file from fv %u, type %u\n",
          *Size, Index, FileType
          ));
        FreePool (Handles);
        return EFI_SUCCESS;
      }

      DEBUG ((EFI_D_WARN, "payload: fv %u ReadFile: %r\n", Index, Status));
    }
  }

  FreePool (Handles);
  DEBUG ((EFI_D_ERROR, "payload: image not present in any firmware volume\n"));

  return EFI_NOT_FOUND;
}

/**
  Attach the storage stack to the volume and report what actually bound.

  Worth the noise: a volume with no filesystem on it is invisible to the
  boot manager, and the difference between "no driver tried" and "a driver
  tried and rejected the image" is the whole diagnosis.
**/
STATIC
VOID
PacConnectVolume (
  VOID
  )
{
  EFI_STATUS  Status;
  VOID       *Interface;

  if (mDiskHandle == NULL) {
    return;
  }

  Status = gBS->ConnectController (mDiskHandle, NULL, NULL, TRUE);
  DEBUG ((EFI_D_WARN, "payload: connect: %r\n", Status));

  Status = gBS->HandleProtocol (mDiskHandle, &gEfiDiskIoProtocolGuid, &Interface);
  DEBUG ((EFI_D_WARN, "payload: disk io on volume: %r\n", Status));

  Status = gBS->HandleProtocol (
                  mDiskHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  &Interface
                  );
  //
  // Not an error when this says unsupported: the partition driver puts a
  // child handle over the volume and the filesystem mounts on the child.
  // The block read count is the useful part, since it proves the storage
  // stack actually looked at the media.
  //
  DEBUG ((
    EFI_D_WARN,
    "payload: filesystem on volume: %r, after %u block reads\n",
    Status, mReadCount
    ));
}

STATIC
VOID
EFIAPI
PacEndOfDxe (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  DEBUG ((EFI_D_WARN, "payload: retrying connect at end of dxe\n"));
  PacConnectVolume ();
}

EFI_STATUS
EFIAPI
PacPayloadVolumeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS   Status;
  EFI_HANDLE   Handle;
  EFI_EVENT    Event;
  VOID        *Section;
  UINTN        Size;

  Section = NULL;
  Size    = 0;
  Handle  = NULL;

  Status = PacReadPayloadFromFv (&Section, &Size);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (Size < PAC_PAYLOAD_BLOCK_SIZE) {
    DEBUG ((EFI_D_ERROR, "payload: %u bytes is too small to be a volume\n", Size));
    FreePool (Section);
    return EFI_VOLUME_CORRUPTED;
  }

  //
  // Use the section buffer as the disk directly. ReadSection already returns
  // a fresh pool copy rather than a window onto the firmware volume, so it
  // is ours to keep and write to, and allocating a second copy only doubled
  // the memory needed at a point in DXE where that was enough to fail.
  //
  mDisk.Buffer = (UINT8 *)Section;
  mDisk.Size   = Size;

  mDisk.Media.MediaId          = 1;
  //
  // Removable on purpose. The boot manager only synthesises a boot option
  // for the default loader path, EFI\BOOT\BOOTAA64.EFI, on removable media;
  // for fixed disks it expects the OS to have registered its own boot
  // variable. This volume ships with exactly that default path on it.
  //
  mDisk.Media.RemovableMedia   = TRUE;
  mDisk.Media.MediaPresent     = TRUE;
  mDisk.Media.LogicalPartition = FALSE;
  mDisk.Media.ReadOnly         = FALSE;
  mDisk.Media.WriteCaching     = FALSE;
  mDisk.Media.BlockSize        = PAC_PAYLOAD_BLOCK_SIZE;
  mDisk.Media.IoAlign          = 0;
  mDisk.Media.LastBlock        = (Size / PAC_PAYLOAD_BLOCK_SIZE) - 1;

  mDisk.BlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION;
  mDisk.BlockIo.Media       = &mDisk.Media;
  mDisk.BlockIo.Reset       = PacDiskReset;
  mDisk.BlockIo.ReadBlocks  = PacDiskReadBlocks;
  mDisk.BlockIo.WriteBlocks = PacDiskWriteBlocks;
  mDisk.BlockIo.FlushBlocks = PacDiskFlushBlocks;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiDevicePathProtocolGuid, &mDevicePath,
                  &gEfiBlockIoProtocolGuid,    &mDisk.BlockIo,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "payload: could not install block io! %r\n", Status));
    return Status;
  }

  DEBUG ((
    EFI_D_WARN,
    "payload: volume published, %u bytes, %lu blocks of %u\n",
    Size,
    mDisk.Media.LastBlock + 1,
    PAC_PAYLOAD_BLOCK_SIZE
    ));

  mDiskHandle = Handle;
  PacConnectVolume ();

  //
  // Connect again at end of DXE. The first attempt above can be too early:
  // a driver that has not been dispatched yet has no driver binding protocol
  // to offer, so it is never asked about this handle and never retried. By
  // end of DXE every driver in the image has had its chance to register.
  //
  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  PacEndOfDxe,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &Event
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "payload: no end of dxe event! %r\n", Status));
  }

  return EFI_SUCCESS;
}
