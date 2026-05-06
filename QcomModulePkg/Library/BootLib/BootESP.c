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
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Uefi.h>
#include <Library/MenuKeysDetection.h>
#include <Library/FastbootMenu.h>
#include <Protocol/DiskIo.h>
#include <Guid/EventGroup.h>
#include <Protocol/DevicePath.h>
#include <Protocol/EFIDisplayPwr.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/EFIDisplayUtils.h>
#include <Library/DrawUI.h>

STATIC CONST CHAR16 BootA64Path[] = L"\\EFI\\BOOT\\BOOTAA64.EFI";
STATIC CONST EFI_GUID gEfiEventDetectSDCardGuid = {
    0xb7972c36,
    0x8a4c,
    0x4a56,
    {0x8b, 0x02, 0x11, 0x59, 0xb5, 0x2d, 0x4b, 0xfb}};

STATIC EFI_DISPLAY_POWER_PROTOCOL *mDisplayPowerProtocol = NULL;

// Initialize the Display Power Protocol if available
STATIC
EFI_STATUS
InitializeDisplayPowerProtocol (VOID)
{
  EFI_STATUS Status = EFI_SUCCESS;

  mDisplayPowerProtocol = NULL;
  Status = gBS->LocateProtocol (&gEfiDisplayPowerStateProtocolGuid, NULL,
                                (VOID **)&mDisplayPowerProtocol);
  if (EFI_ERROR (Status) || mDisplayPowerProtocol == NULL) {
    DEBUG ((DEBUG_INFO,
            "InitializeDisplayPowerProtocol: no display power protocol: %r\n",
            Status));
    mDisplayPowerProtocol = NULL;
    return Status;
  }

  // query current state and try a test set to ensure it's usable
  EFI_DISPLAY_POWER_STATE CurState = EfiDisplayPowerStateUnknown;
  Status = mDisplayPowerProtocol->GetDisplayPowerState (mDisplayPowerProtocol,
                                                        &CurState);
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "InitializeDisplayPowerProtocol: current state=%d\n",
            CurState));
  }

  return Status;
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
RegisterExitBootServicesDisplayCallback (
  EFI_EVENT *Event
){
  EFI_STATUS Status;

  Status = gBS->CreateEvent (EVT_SIGNAL_EXIT_BOOT_SERVICES, TPL_CALLBACK,
                             DisableDisplayOnExitBootServices, NULL, Event);
  if (EFI_ERROR (Status) || *Event == NULL) {
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

EFIAPI
VOID
SignalSDDetection(VOID){
  SignalGuidEvent (&gEfiEventDetectSDCardGuid);
  ConnectAllControllers ();
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
  // gBS->RestoreTPL (TPL_APPLICATION);

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

EFI_STATUS
EFIAPI
CheckBootAA64(){
  EFI_STATUS Status = EFI_NOT_FOUND;
  EFI_HANDLE *Handles = NULL;
  UINTN HandleCount = 0;
  UINTN Idx;

  DEBUG((DEBUG_INFO, "CheckBootAA64: try find bootable image\n"));
  
  Status =
      gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                               NULL, &HandleCount, &Handles);
  if (EFI_ERROR (Status) || HandleCount == 0 || Handles == NULL) {
    DEBUG ((DEBUG_INFO,
            "CheckBootAA64: no SimpleFileSystem handles: %r\n", Status));
    return EFI_NOT_FOUND;
  }

  for (Idx = 0; Idx < HandleCount; Idx++) {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
    EFI_FILE_PROTOCOL *Root = NULL;
    EFI_FILE_PROTOCOL *File = NULL;
    EFI_DEVICE_PATH_PROTOCOL *BootFileDevicePath = NULL;

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
      DEBUG ((DEBUG_INFO, "CheckBootAA64: FileDevicePath failed\n"));
      continue;
    }

    // Found BOOTAA64.EFI
    Status = EFI_SUCCESS;

    FreePool (BootFileDevicePath);
    goto free;
  }

free:
  gBS->FreePool (Handles);
  if (EFI_ERROR(Status)) {
    DEBUG ((DEBUG_INFO, "CheckBootAA64: BOOTAA64.EFI not found: %r\n", Status));
  } else {
    DEBUG ((DEBUG_INFO, "CheckBootAA64: BOOTAA64.EFI found\n"));
  }
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
  EFI_EVENT DisplayPowerEvent = NULL;
  // Backup Current TPL
  EFI_TPL CurrentTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  // Reduce TPL to Application for further operations
  gBS->RestoreTPL (TPL_APPLICATION);

  // Only Register & Signal once
  STATIC BOOLEAN FirstCall = TRUE;
  if (FirstCall) {
    FirstCall = FALSE;
    // Initialize display protocol and register ExitBootServices callback
    SignalGuidEvent (&gEfiEventReadyToBootGuid);
    SignalGuidEvent (&gEfiEndOfDxeEventGroupGuid);
  }

  if (!EFI_ERROR(InitializeDisplayPowerProtocol ())) {
    RegisterExitBootServicesDisplayCallback(&DisplayPowerEvent);
  }

  // Signal SD detection event
  SignalSDDetection();
  
  // Load and start BOOTAA64.EFI
  Status = LoadBootA64AndStart ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "BootESP: LoadBootA64AndStart failed: %r\n", Status));
  }

  // If boot failure, unregister the display power protocol
  if (EFI_ERROR (Status) && DisplayPowerEvent != NULL) {
    gBS->CloseEvent (DisplayPowerEvent);
    DisplayPowerEvent = NULL;
  }

  // Restore original TPL
  gBS->RaiseTPL (CurrentTPL);

  return Status;
}

BOOLEAN
EFIAPI
CheckSdAndESP (VOID)
{
    EFI_STATUS Status;
    EFI_HANDLE *HandleBuffer = NULL;
    UINTN HandleCount = 0;
    BOOLEAN SDPresent = FALSE;
    // Backup Current TPL
    EFI_TPL CurrentTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
    // Reduce TPL to Application for further operations
    gBS->RestoreTPL (TPL_APPLICATION);

    EFI_HANDLE *TempHandles = NULL;
    UINTN HandleCountBefore = 0;

    // Get handle count before signaling SD detection
    Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                      NULL, &HandleCountBefore, &TempHandles);
    if (TempHandles != NULL) {
      gBS->FreePool (TempHandles);
      TempHandles = NULL;
    }

    DEBUG ((DEBUG_INFO, "ScanSD: HandleCount before signal = %u\n", (UINT32)HandleCountBefore));

    // Signal SD detection event and connect controllers so new volumes can appear
    SignalSDDetection();

    // Get handle count after signaling
    Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                      NULL, &HandleCount, &HandleBuffer);
    if (EFI_ERROR (Status) || HandleCount == 0 || HandleBuffer == NULL) {
      DEBUG ((DEBUG_INFO, "ScanSD: no SimpleFileSystem handles after signal: %r\n", Status));
      Status = EFI_NOT_FOUND;
      goto cleanup;
    }

    DEBUG ((DEBUG_INFO, "ScanSD: HandleCount after signal = %u\n", (UINT32)HandleCount));

    if (HandleCount > HandleCountBefore) {
      SDPresent = TRUE;
      DEBUG ((DEBUG_INFO, "ScanSD: Detected SD/MMC\n"));
    }

    if (HandleBuffer != NULL) {
        gBS->FreePool (HandleBuffer);
        DEBUG((DEBUG_INFO, "ScanSD: Freed HandleBuffer %p\n", HandleBuffer));
    }

    DEBUG((DEBUG_INFO, "ScanSD: SDPresent=%d\n", SDPresent));

    // If SD is present, check for BootAA64.EFI in ESP
    // no matter ESP Partition on ufs or SD, grub will find available rootfs by default.
    // Rootfs on SD card has higher priority.
    SDPresent = SDPresent && !EFI_ERROR(CheckBootAA64());
cleanup:
    // Restore original TPL
    gBS->RaiseTPL (CurrentTPL);
    return SDPresent;
}

STATIC EFI_STATUS
DisplaySetVariable (CHAR16 *VariableName, VOID *VariableValue, UINTN DataSize)
{
  EFI_STATUS Status = EFI_SUCCESS;
  BOOLEAN RTVariable = FALSE;
  EfiQcomDisplayUtilsProtocol *pDisplayUtilsProtocol = NULL;

  Status = gBS->LocateProtocol (&gQcomDisplayUtilsProtocolGuid,
                                NULL,
                                (VOID **)&pDisplayUtilsProtocol);
  if ((EFI_ERROR (Status)) ||
      (pDisplayUtilsProtocol == NULL)) {
    RTVariable = TRUE;
  } else if (pDisplayUtilsProtocol->Revision <  0x20000) {
    RTVariable = TRUE;
  } else {
    /* The display utils version for 0x20000 and above can support
       display protocol to get and set variable */
    Status = pDisplayUtilsProtocol->DisplayUtilsSetVariable (
          VariableName,
          (UINT8 *)VariableValue,
          DataSize,
          0);
  }

  if (RTVariable) {
    Status = gRT->SetVariable (VariableName,
                               &gQcomTokenSpaceGuid,
                               EFI_VARIABLE_RUNTIME_ACCESS |
                               EFI_VARIABLE_BOOTSERVICE_ACCESS |
                               EFI_VARIABLE_NON_VOLATILE,
                               DataSize,
                               (VOID *)VariableValue);
  }

  if (Status == EFI_NOT_FOUND) {
    // EFI_NOT_FOUND is not an error for retail case.
    Status = EFI_SUCCESS;
  } else if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_VERBOSE,
        "Display set variable failed with status(%d)!\n", Status));
  }

  return Status;
}

STATIC EFI_STATUS
DisplayGetVariable (CHAR16 *VariableName, VOID *VariableValue, UINTN *DataSize)
{
  EFI_STATUS Status = EFI_SUCCESS;
  BOOLEAN RTVariable = FALSE;
  EfiQcomDisplayUtilsProtocol *pDisplayUtilsProtocol = NULL;

  Status = gBS->LocateProtocol (&gQcomDisplayUtilsProtocolGuid,
                                NULL,
                                (VOID **)&pDisplayUtilsProtocol);
  if ((EFI_ERROR (Status)) ||
      (pDisplayUtilsProtocol == NULL)) {
    RTVariable = TRUE;
  } else if (pDisplayUtilsProtocol->Revision <  0x20000) {
    RTVariable = TRUE;
  } else {
    /* The display utils version for 0x20000 and above can support
       display protocol to get and set variable */
    Status = pDisplayUtilsProtocol->DisplayUtilsGetVariable (
          VariableName,
          (UINT8 *)VariableValue,
          DataSize,
          0);
  }

  if (RTVariable) {
    Status = gRT->GetVariable (VariableName,
                               &gQcomTokenSpaceGuid,
                               NULL,
                               DataSize,
                               (VOID *)VariableValue);
  }

  if (Status == EFI_NOT_FOUND) {
    // EFI_NOT_FOUND is not an error for retail case.
    Status = EFI_SUCCESS;
  } else if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_VERBOSE,
        "Display get variable failed with status(%d)!\n", Status));
  }

  return Status;
}


EFI_STATUS
EFIAPI
SetBootPath(
  IN BOOT_PATH BootESP
){
  EFI_STATUS Status = EFI_SUCCESS;

  // Check validity
  if (BootESP != BOOT_PATH_ANDROID && BootESP != BOOT_PATH_ESP) {
    DEBUG((DEBUG_WARN, "SetBootPath: Invalid BootESP value %u, set to android\n", BootESP));
    BootESP = BOOT_PATH_ANDROID;
  }

  Status = DisplaySetVariable((CHAR16 *)L"OSBootPath",
                              &BootESP,
                              sizeof(BootESP));
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "SetBootPath: Failed to set OSBootPath variable: %r\n", Status));
  } else {
    DEBUG((DEBUG_INFO, "SetBootPath: OSBootPath saved = %u\n", BootESP));
  }

  return Status;
}

BOOT_PATH
EFIAPI
ReadBootPath(VOID)
{
  EFI_STATUS Status = EFI_SUCCESS;
  BOOT_PATH BootESP = BOOT_PATH_UNKNOWN;
  UINTN Size = sizeof(BootESP);

  Status = DisplayGetVariable(L"OSBootPath", &BootESP, &Size);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_INFO, "ReadBootPath: Failed to read OSBootPath variable: %r\n", Status));
    if (Status == EFI_NOT_FOUND) {
      // Variable does not exist: use default = BOOT_PATH_ANDROID
      DEBUG((DEBUG_INFO, "ReadBootPath: OSBootPath variable not found, default to android\n"));
      SetBootPath(BOOT_PATH_ANDROID);
    }
    BootESP = BOOT_PATH_ANDROID; // Default to Android
  }

  // Check validity
  if (BootESP != BOOT_PATH_ANDROID && BootESP != BOOT_PATH_ESP) {
    DEBUG((DEBUG_WARN, "ReadBootPath: Invalid OSBootPath value %u, reset to android\n", BootESP));
    BootESP = BOOT_PATH_ANDROID;
    SetBootPath(BOOT_PATH_ANDROID);
  }

  DEBUG((DEBUG_INFO, "ReadBootPath: OSBootPath read = %u\n", BootESP));
  return BootESP;
}

EFI_STATUS
EFIAPI
ToggleBootPath(VOID)
{
  EFI_STATUS Status = EFI_SUCCESS;
  // Backup Current TPL
  EFI_TPL CurrentTPL = gBS->RaiseTPL (TPL_HIGH_LEVEL);
  // Reduce TPL to Application for further operations
  gBS->RestoreTPL (TPL_APPLICATION);

  BOOT_PATH CurrentBootPath = ReadBootPath();

  // Toggle between Android and ESP
  if (CurrentBootPath == BOOT_PATH_ANDROID) {
    CurrentBootPath = BOOT_PATH_ESP;
  } else {
    CurrentBootPath = BOOT_PATH_ANDROID;
  }

  // Set the new boot path
  Status = SetBootPath(CurrentBootPath);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "ToggleBootPath: Failed to set new boot path: %r\n", Status));
    return Status;
  }

  DEBUG((DEBUG_INFO, "ToggleBootPath: Boot path toggled to %u\n", CurrentBootPath));

  gBS->RaiseTPL (CurrentTPL);
  return Status;
}
