#ifndef __BOOTESP_H__
#define __BOOTESP_H__

#include <Uefi.h>
#include <Library/BaseLib.h>

typedef enum {
  BOOT_PATH_ANDROID = 0,
  BOOT_PATH_ESP = 1,
  BOOT_PATH_UNKNOWN = 0xFFFFFFFF // UINT32
} BOOT_PATH;

EFI_STATUS
EFIAPI
BootESP (VOID);

BOOLEAN
EFIAPI
CheckSdAndESP (VOID);

EFI_STATUS
EFIAPI
CheckBootAA64();

EFIAPI
VOID
SignalSDDetection(VOID);

EFI_STATUS
EFIAPI
SetBootPath(
    IN BOOT_PATH BootESP
);

BOOT_PATH
EFIAPI
ReadBootPath(VOID);

EFI_STATUS
EFIAPI
ToggleBootPath(VOID);


#endif // __BOOTESP_H__