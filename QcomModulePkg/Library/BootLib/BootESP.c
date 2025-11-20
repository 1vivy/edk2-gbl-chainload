/**
  BootESP - load and start \EFI\BOOT\BOOTAA64.EFI from any mounted FS.

  This function is intended to be called from ABL to transfer control
  to the ESP boot loader. It performs the following high-level steps:
  - Connect all controllers so that file systems are available
  - Signal a few firmware GUID events (Detect SD, ReadyToBoot, EndOfDxe)
  - Search all SimpleFileSystem volumes for "\\EFI\\BOOT\\BOOTAA64.EFI"
  - Call LoadImage/StartImage directly using a synthesized device path

*/
#include <Library/BaseLib.h>
#include <Library/BootESP.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Uefi.h>

#include <Guid/EventGroup.h>
#include <Protocol/DevicePath.h>
#include <Protocol/EFIDisplayPwr.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/MenuKeysDetection.h>
#include <Library/FastbootMenu.h>

STATIC CONST CHAR16 BootA64Path[] = L"\\EFI\\BOOT\\BOOTAA64.EFI";
STATIC CONST EFI_GUID gEfiEventDetectSDCardGuid = {
    0xb7972c36,
    0x8a4c,
    0x4a56,
    {0x8b, 0x02, 0x11, 0x59, 0xb5, 0x2d, 0x4b, 0xfb}};

STATIC EFI_DISPLAY_POWER_PROTOCOL *mDisplayPowerProtocol = NULL;

// Initialize the Display Power Protocol if available
STATIC
VOID
InitializeDisplayPowerProtocol (VOID)
{
  EFI_STATUS Status;

  mDisplayPowerProtocol = NULL;
  Status = gBS->LocateProtocol (&gEfiDisplayPowerStateProtocolGuid, NULL,
                                (VOID **)&mDisplayPowerProtocol);
  if (EFI_ERROR (Status) || mDisplayPowerProtocol == NULL) {
    DEBUG ((DEBUG_INFO,
            "InitializeDisplayPowerProtocol: no display power protocol: %r\n",
            Status));
    mDisplayPowerProtocol = NULL;
    return;
  }

  // query current state and try a test set to ensure it's usable
  EFI_DISPLAY_POWER_STATE CurState = EfiDisplayPowerStateUnknown;
  Status = mDisplayPowerProtocol->GetDisplayPowerState (mDisplayPowerProtocol,
                                                        &CurState);
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "InitializeDisplayPowerProtocol: current state=%d\n",
            CurState));
    // Try to set On briefly to validate Set function (ignore errors)
    (void)mDisplayPowerProtocol->SetDisplayPowerState (mDisplayPowerProtocol,
                                                       EfiDisplayPowerStateOn);
  }
}

// ExitBootServices notify callback to turn display off
STATIC
VOID EFIAPI
DisableDisplayOnExitBootServices (IN EFI_EVENT Event, IN VOID *Context)
{
  if (mDisplayPowerProtocol != NULL) {
    (void)mDisplayPowerProtocol->SetDisplayPowerState (mDisplayPowerProtocol,
                                                       EfiDisplayPowerStateOff);
  }
}

// Register callback for ExitBootServices to disable display
STATIC
VOID
RegisterExitBootServicesDisplayCallback (VOID)
{
  EFI_STATUS Status;
  EFI_EVENT Event = NULL;

  Status = gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_CALLBACK,
                             DisableDisplayOnExitBootServices, NULL, &Event);
  if (EFI_ERROR (Status) || Event == NULL) {
    DEBUG ((DEBUG_INFO,
            "RegisterExitBootServicesDisplayCallback: CreateEvent failed: %r\n",
            Status));
    return;
  }

  // keep event open so it fires at ExitBootServices
  DEBUG ((DEBUG_INFO, "RegisterExitBootServicesDisplayCallback: registered\n"));
}

// Connect all handles/controllers so file systems become available
STATIC
VOID
ConnectAllControllers (VOID)
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN HandleCount = 0;

  Status =
      gBS->LocateHandleBuffer (AllHandles, NULL, NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO,
            "ConnectAllControllers: no handles found or LocateHandleBuffer "
            "failed: %r\n",
            Status));
    return;
  }

  for (UINTN i = 0; i < HandleCount; i++) {
    (void)gBS->ConnectController (Handles[i], NULL, NULL, TRUE);
  }

  gBS->FreePool (Handles);
}

// Create and signal a simple event-ex (notify-signal) for the given GUID
STATIC
VOID EFIAPI
SignalGuidEventCallback (IN EFI_EVENT Event, IN VOID *Context)
{
  DEBUG ((EFI_D_INFO, "SignalGuidEventCallback: signaled event\n"));
  return;
}

STATIC
VOID
SignalGuidEvent (CONST EFI_GUID *EventGuid)
{
  EFI_STATUS Status;
  EFI_EVENT Event = NULL;

  Status =
      gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                          SignalGuidEventCallback, NULL, EventGuid, &Event);
  if (EFI_ERROR (Status) || Event == NULL) {
    DEBUG ((DEBUG_INFO, "SignalGuidEvent: CreateEventEx failed for %g: %r\n",
            EventGuid, Status));
    return;
  }

  gBS->SignalEvent (Event);
  gBS->CloseEvent (Event);
}

// Try to find and load BOOTAA64 from any simple file system.
STATIC
EFI_STATUS
EFIAPI
LoadBootA64AndStart()
{
  EFI_STATUS Status;
  EFI_HANDLE *Handles = NULL;
  UINTN HandleCount = 0;
  UINTN Idx;
  gBS->RestoreTPL (TPL_APPLICATION);

  DEBUG((DEBUG_INFO, "LoadBootA64AndStart: try find bootable image\n"));
  
  Status =
      gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                               NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status) || HandleCount == 0 || Handles == NULL) {
    DEBUG ((DEBUG_INFO,
            "LoadBootA64AndStart: no SimpleFileSystem handles: %r\n", Status));
    return EFI_NOT_FOUND;
  }

  for (Idx = 0; Idx < HandleCount; Idx++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_DEVICE_PATH_PROTOCOL *BootFileDevicePath = NULL;
    EFI_HANDLE ImageHandle = NULL;

    Status = gBS->HandleProtocol (
        Handles[Idx], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
    if (EFI_ERROR (Status) || (Fs == NULL)) {
      continue;
    }

    Status = Fs->OpenVolume (Fs, &Root);
    if (EFI_ERROR (Status) || (Root == NULL)) {
      continue;
    }

    Status =
        Root->Open (Root, &File, (CHAR16 *)BootA64Path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR (Status) || (File == NULL)) {
      Root->Close (Root);
      Root = NULL;
      continue;
    }

    File->Close (File);
    File = NULL;
    Root->Close (Root);
    Root = NULL;

    BootFileDevicePath = FileDevicePath (Handles[Idx], BootA64Path);
    if (BootFileDevicePath == NULL) {
      DEBUG ((DEBUG_INFO, "LoadBootA64AndStart: FileDevicePath failed\n"));
      continue;
    }

    // Use BootPolicy = TRUE to leverage firmware Boot Manager heuristics
    Status = gBS->LoadImage (FALSE, gImageHandle, BootFileDevicePath, NULL, 0,
                             &ImageHandle);
    if (EFI_ERROR (Status)) {
      DEBUG (
          (DEBUG_INFO, "LoadBootA64AndStart: LoadImage failed: %r\n", Status));
      FreePool (BootFileDevicePath);
      continue;
    }

    Status = gBS->StartImage (ImageHandle, NULL, NULL);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "LoadBootA64AndStart: StartImage failed: %r\n", Status));
      (VOID) gBS->UnloadImage (ImageHandle);
      FreePool (BootFileDevicePath);
      goto free;
    }

    FreePool (BootFileDevicePath);
    goto free;
  }

free:
  gBS->FreePool (Handles);
  return Status;
}

/**
  Entry point called from ABL to boot ESP image.
*/
EFI_STATUS
EFIAPI
BootESP (VOID)
{
  EFI_STATUS Status = EFI_SUCCESS;
  ExitMenuKeysDetection();
  // Backup Current TPL
  EFI_TPL CurrentTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  // Reduce TPL to Application for further operations
  gBS->RestoreTPL (TPL_APPLICATION);

  // Only Register & Signal once
  STATIC BOOLEAN FirstCall = TRUE;
  if (FirstCall) {
    FirstCall = FALSE;
    // Initialize display protocol and register ExitBootServices callback
    InitializeDisplayPowerProtocol ();
    RegisterExitBootServicesDisplayCallback ();

    SignalGuidEvent (&gEfiEventReadyToBootGuid);
    SignalGuidEvent (&gEfiEndOfDxeEventGroupGuid);
  }

  // Signal a few well-known GUID events
  SignalGuidEvent (&gEfiEventDetectSDCardGuid);
  ConnectAllControllers ();

  // Load and start BOOTAA64.EFI
  Status = LoadBootA64AndStart ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "BootESP: LoadBootA64AndStart failed: %r\n", Status));
  }

  // Restore original TPL
  gBS->RaiseTPL (CurrentTPL);
  DisplayFastbootMenu ();
  return Status;
}
