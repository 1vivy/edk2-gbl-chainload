#ifndef __BOOTESP_H__
#define __BOOTESP_H__

#include <Uefi.h>
#include <Library/BaseLib.h>

EFI_STATUS
EFIAPI
BootESP (VOID);

BOOLEAN
EFIAPI
CheckSdAndESP (VOID);

#endif // __BOOTESP_H__