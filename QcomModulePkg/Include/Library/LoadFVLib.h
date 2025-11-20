/**
  LoadFVLib.h

  Public header for the LoadFV library which provides helpers to
  enumerate a firmware volume (FV) and load/start PE32 driver images
  from it.

*/

#ifndef __LOAD_FV_LIB_H__
#define __LOAD_FV_LIB_H__

#include <Uefi.h>
#include <Pi/PiFirmwareFile.h>
#include <Pi/PiFirmwareVolume.h>
#include <Library/BaseLib.h>
#include <Library/Debug.h>
#include <Library/DebugLib.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/FirmwareVolume2.h>
#include <Library/UefiBootServicesTableLib.h>

/**
  Load all driver-type files from the firmware volume that contains the
  provided ImageHandle's DeviceHandle and start them.

  @param ImageHandle  Parent image handle used for LoadImage/StartImage.

  @retval EFI_SUCCESS Always returns EFI_SUCCESS unless allocation/protocol calls fail.
*/
EFI_STATUS
LoadDriversFromCurrentFv(
  IN EFI_HANDLE ImageHandle
  );

/**
  Load all driver-type files from a single firmware volume and start them.

  @param ImageHandle   Parent image handle used for LoadImage/StartImage.
  @param Fv            Pointer to firmware volume protocol.

  @retval EFI_SUCCESS  Operation completed (individual load errors are logged).
*/
EFI_STATUS
LoadDriversFromFv(
  IN EFI_HANDLE ImageHandle,
  IN EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv
  );

#endif // __LOAD_FV_LIB_H__
