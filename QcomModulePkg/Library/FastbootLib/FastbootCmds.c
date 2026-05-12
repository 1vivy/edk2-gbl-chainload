/** @file

  Copyright (c) 2013-2014, ARM Ltd. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD
License
  which accompanies this distribution.  The full text of the license may be
found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

/*
 * Copyright (c) 2009, Google Inc.
 * All rights reserved.
 *
 * Copyright (c) 2015 - 2021, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * * Redistributions of source code must retain the above copyright
 *  notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 *  with the distribution.
 *   * Neither the name of The Linux Foundation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/*
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/Debug.h>
#include <Library/DeviceInfo.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MenuKeysDetection.h>
#include <Library/PartitionTableUpdate.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/ThreadStack.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UnlockMenu.h>
#include <Library/BootLinux.h>
#include <Uefi.h>

#include <Guid/EventGroup.h>

#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>

#if defined (AUTO_DEBUG_MODE) || defined (MODE_DEBUG) || defined (MODE_TEMPLATE) || defined (FAKELOCKED) || defined (FAKELOCKED_DEBUG)
#define GBL_EXPERIMENTAL_FASTBOOT_CMDS 1
#endif
#if defined (GBL_EXPERIMENTAL_FASTBOOT_CMDS)
EFI_STATUS EFIAPI BootFlowChainLoad (VOID);
#include <Library/AvbParseLib.h>
#endif
#include <Protocol/DiskIo.h>
#include <Protocol/EFIUsbDevice.h>
#include <Protocol/EFIUbiFlasher.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>
#include <Protocol/EFIDisplayUtils.h>

#include "AutoGen.h"
#include "BootImage.h"
#include "BootLinux.h"
#include "BootStats.h"
#include "FastbootCmds.h"
#include "FastbootMain.h"
#include "LinuxLoaderLib.h"
#include "MetaFormat.h"
#include "SparseFormat.h"
#include "Recovery.h"

STATIC struct GetVarPartitionInfo part_info[] = {
    {"system", "partition-size:", "partition-type:", "", "ext4"},
    {"userdata", "partition-size:", "partition-type:", "", "ext4"},
    {"cache", "partition-size:", "partition-type:", "", "ext4"},
};

STATIC struct GetVarPartitionInfo PublishedPartInfo[MAX_NUM_PARTITIONS];

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
STATIC CONST CHAR16 *CriticalPartitions[] = {
    L"abl",  L"rpm",        L"tz",      L"sdi",       L"xbl",       L"hyp",
    L"pmic", L"bootloader", L"devinfo", L"partition", L"devcfg",    L"ddr",
    L"frp",  L"cdt",        L"cmnlib",  L"cmnlib64",  L"keymaster", L"mdtp",
    L"aop",  L"multiimgoem", L"secdata", L"imagefv",  L"qupfw", L"uefisecapp"};

STATIC BOOLEAN
IsCriticalPartition (CHAR16 *PartitionName);

STATIC CONST CHAR16 *VirtualAbCriticalPartitions[] = {
    L"misc",  L"metadata",  L"userdata"};

STATIC BOOLEAN
CheckVirtualAbCriticalPartition (CHAR16 *PartitionName);
#endif

STATIC FASTBOOT_VAR *Varlist;
STATIC BOOLEAN Finished = FALSE;
STATIC CHAR8 StrSerialNum[MAX_RSP_SIZE];
STATIC CHAR8 FullProduct[MAX_RSP_SIZE];
STATIC CHAR8 StrVariant[MAX_RSP_SIZE];
STATIC CHAR8 StrBatteryVoltage[MAX_RSP_SIZE];
STATIC CHAR8 StrBatterySocOk[MAX_RSP_SIZE];
STATIC CHAR8 ChargeScreenEnable[MAX_RSP_SIZE];
STATIC CHAR8 OffModeCharge[MAX_RSP_SIZE];
STATIC CHAR8 StrSocVersion[MAX_RSP_SIZE];
STATIC CHAR8 LogicalBlkSizeStr[MAX_RSP_SIZE];
STATIC CHAR8 EraseBlkSizeStr[MAX_RSP_SIZE];
STATIC CHAR8 MaxDownloadSizeStr[MAX_RSP_SIZE];
STATIC CHAR8 SnapshotMergeState[MAX_RSP_SIZE];

struct GetVarSlotInfo {
  CHAR8 SlotSuffix[MAX_SLOT_SUFFIX_SZ];
  CHAR8 SlotSuccessfulVar[SLOT_ATTR_SIZE];
  CHAR8 SlotUnbootableVar[SLOT_ATTR_SIZE];
  CHAR8 SlotRetryCountVar[SLOT_ATTR_SIZE];
  CHAR8 SlotSuccessfulVal[ATTR_RESP_SIZE];
  CHAR8 SlotUnbootableVal[ATTR_RESP_SIZE];
  CHAR8 SlotRetryCountVal[ATTR_RESP_SIZE];
};

STATIC struct GetVarSlotInfo *BootSlotInfo = NULL;
STATIC CHAR8 SlotSuffixArray[SLOT_SUFFIX_ARRAY_SIZE];
STATIC CHAR8 SlotCountVar[ATTR_RESP_SIZE];
STATIC CHAR8 CurrentSlotFB[MAX_SLOT_SUFFIX_SZ];

/*Note: This needs to be used only when Slot already has prefix "_" */
#define SKIP_FIRSTCHAR_IN_SLOT_SUFFIX(Slot)                                    \
  do {                                                                         \
    int i = 0;                                                                 \
    do {                                                                       \
      Slot[i] = Slot[i + 1];                                                   \
      i++;                                                                     \
    } while (i < MAX_SLOT_SUFFIX_SZ - 1);                                      \
  } while (0);

#define MAX_DISPLAY_PANEL_OVERRIDE 256
#define MAX_GPU_CONFIG_OVERRIDE 256

/*This variable is used to skip populating the FastbootVar
 * When PopulateMultiSlotInfo called while flashing each Lun
 */
STATIC BOOLEAN InitialPopulate = FALSE;
STATIC UINT32 SlotCount;
extern struct PartitionEntry PtnEntries[MAX_NUM_PARTITIONS];

STATIC ANDROID_FASTBOOT_STATE mState = ExpectCmdState;
/* When in ExpectDataState, the number of bytes of data to expect: */
STATIC UINT64 mNumDataBytes;
STATIC UINT64 mFlashNumDataBytes;
/* .. and the number of bytes so far received this data phase */
STATIC UINT64 mBytesReceivedSoFar;
/*  and the buffer to save data into */
STATIC UINT8 *mDataBuffer = NULL;
/*  and the offset for usb to save data into */
STATIC UINT8 *mFlashDataBuffer = NULL;
STATIC UINT8 *mUsbDataBuffer = NULL;

STATIC EFI_KERNEL_PROTOCOL  *KernIntf = NULL;
STATIC BOOLEAN IsMultiThreadSupported = FALSE;
STATIC BOOLEAN IsFlashComplete = TRUE;
STATIC LockHandle *LockDownload;
STATIC LockHandle *LockFlash;

STATIC EFI_STATUS FlashResult = EFI_SUCCESS;
#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
STATIC EFI_EVENT UsbTimerEvent;
#endif

STATIC UINT64 MaxDownLoadSize = 0;

STATIC INT32 Lun = NO_LUN;
STATIC BOOLEAN LunSet;

STATIC FASTBOOT_CMD *cmdlist;
STATIC UINT32 IsAllowUnlock;

STATIC EFI_STATUS
FastbootCommandSetup (VOID *Base, UINT64 Size);
STATIC VOID
AcceptCmd (IN UINT64 Size, IN CHAR8 *Data);
STATIC VOID
AcceptCmdHandler (IN EFI_EVENT Event, IN VOID *Context);

#define NAND_PAGES_PER_BLOCK 64

#define UBI_HEADER_MAGIC "UBI#"
#define UBI_NUM_IMAGES 1
typedef struct UbiHeader {
  CHAR8 HdrMagic[4];
} UbiHeader_t;

typedef struct {
  UINT64 Size;
  VOID *Data;
} CmdInfo;

typedef struct {
  CHAR16 PartitionName[MAX_GPT_NAME_SIZE];
  UINT32 PartitionSize;
  UINT8 *FlashDataBuffer;
  UINT64 FlashNumDataBytes;
} FlashInfo;

STATIC BOOLEAN FlashSplitNeeded;
STATIC BOOLEAN UsbTimerStarted;

BOOLEAN IsUsbTimerStarted (VOID) {
  return UsbTimerStarted;
}

BOOLEAN IsFlashSplitNeeded (VOID)
{
  if (IsUseMThreadParallel ()) {
    return FlashSplitNeeded;
  } else {
    return UsbTimerStarted;
  }
}

BOOLEAN FlashComplete (VOID)
{
  return IsFlashComplete;
}

#ifdef DISABLE_PARALLEL_DOWNLOAD_FLASH
BOOLEAN IsDisableParallelDownloadFlash (VOID)
{
  return TRUE;
}
#else
BOOLEAN IsDisableParallelDownloadFlash (VOID)
{
  return FALSE;
}
#endif

/* Clean up memory for the getvar and cmdlist variables
 * during exit.
 */
STATIC EFI_STATUS FastbootUnInit (VOID)
{
  FASTBOOT_VAR *Var;
  FASTBOOT_CMD *cmd;

  while (Varlist) {
    Var = Varlist;
    Varlist = Varlist->next;
    FreePool (Var);
  }

  while (cmdlist) {
    cmd = cmdlist;
    cmdlist = cmdlist->next;
    FreePool (cmd);
  }
  return EFI_SUCCESS;
}

/* Publish a variable readable by the built-in getvar command
 * These Variables must not be temporary, shallow copies are used.
 */
STATIC VOID
FastbootPublishVar (IN CONST CHAR8 *Name, IN CONST CHAR8 *Value)
{
  FASTBOOT_VAR *Var;
  Var = AllocateZeroPool (sizeof (*Var));
  if (Var) {
    Var->next = Varlist;
    Varlist = Var;
    Var->name = Name;
    Var->value = Value;
  } else {
    DEBUG ((EFI_D_VERBOSE,
            "Failed to publish a variable readable(%a): malloc error!\n",
            Name));
  }
}

/* Returns the Remaining amount of bytes expected
 * This lets us bypass ZLT issues
 */
UINTN GetXfrSize (VOID)
{
  UINTN BytesLeft = mNumDataBytes - mBytesReceivedSoFar;
  if ((mState == ExpectDataState) && (BytesLeft < USB_BUFFER_SIZE))
    return BytesLeft;

  return USB_BUFFER_SIZE;
}

/* Acknowlege to host, INFO, OKAY and FAILURE */
STATIC VOID
FastbootAck (IN CONST CHAR8 *code, CONST CHAR8 *Reason)
{
  if (Reason == NULL)
    Reason = "";

  AsciiSPrint (GetFastbootDeviceData ()->gTxBuffer, MAX_RSP_SIZE, "%a%a", code,
               Reason);
  GetFastbootDeviceData ()->UsbDeviceProtocol->Send (
      ENDPOINT_OUT, AsciiStrLen (GetFastbootDeviceData ()->gTxBuffer),
      GetFastbootDeviceData ()->gTxBuffer);
  DEBUG ((EFI_D_VERBOSE, "Sending %d:%a\n",
          AsciiStrLen (GetFastbootDeviceData ()->gTxBuffer),
          GetFastbootDeviceData ()->gTxBuffer));
}

VOID
FastbootFail (IN CONST CHAR8 *Reason)
{
  FastbootAck ("FAIL", Reason);
}

VOID
FastbootInfo (IN CONST CHAR8 *Info)
{
  FastbootAck ("INFO", Info);
}

VOID
FastbootOkay (IN CONST CHAR8 *info)
{
  FastbootAck ("OKAY", info);
}

VOID PartitionDump (VOID)
{
  EFI_STATUS Status;
  EFI_PARTITION_ENTRY *PartEntry;
  UINT16 i;
  UINT32 j;
  /* By default the LunStart and LunEnd would point to '0' and max value */
  UINT32 LunStart = 0;
  UINT32 LunEnd = GetMaxLuns ();

  /* If Lun is set in the Handle flash command then find the block io for that
   * lun */
  if (LunSet) {
    LunStart = Lun;
    LunEnd = Lun + 1;
  }
  for (i = LunStart; i < LunEnd; i++) {
    for (j = 0; j < Ptable[i].MaxHandles; j++) {
      Status =
          gBS->HandleProtocol (Ptable[i].HandleInfoList[j].Handle,
                               &gEfiPartitionRecordGuid, (VOID **)&PartEntry);
      if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_VERBOSE, "Error getting the partition record for Lun %d "
                               "and Handle: %d : %r\n",
                i, j, Status));
        continue;
      }
      DEBUG ((EFI_D_INFO, "Name:[%s] StartLba: %u EndLba:%u\n",
              PartEntry->PartitionName, PartEntry->StartingLBA,
              PartEntry->EndingLBA));
    }
  }
}

EFI_STATUS
PartitionGetInfo (IN CHAR16 *PartitionName,
                  OUT EFI_BLOCK_IO_PROTOCOL **BlockIo,
                  OUT EFI_HANDLE **Handle)
{
  EFI_STATUS Status;
  EFI_PARTITION_ENTRY *PartEntry;
  UINT16 i;
  UINT32 j;
  /* By default the LunStart and LunEnd would point to '0' and max value */
  UINT32 LunStart = 0;
  UINT32 LunEnd = GetMaxLuns ();

  /* If Lun is set in the Handle flash command then find the block io for that
   * lun */
  if (LunSet) {
    LunStart = Lun;
    LunEnd = Lun + 1;
  }
  for (i = LunStart; i < LunEnd; i++) {
    for (j = 0; j < Ptable[i].MaxHandles; j++) {
      Status =
          gBS->HandleProtocol (Ptable[i].HandleInfoList[j].Handle,
                               &gEfiPartitionRecordGuid, (VOID **)&PartEntry);
      if (EFI_ERROR (Status)) {
        continue;
      }
      if (!(StrCmp (PartitionName, PartEntry->PartitionName))) {
        *BlockIo = Ptable[i].HandleInfoList[j].BlkIo;
        *Handle = Ptable[i].HandleInfoList[j].Handle;
        return Status;
      }
    }
  }

  DEBUG ((EFI_D_ERROR, "Partition not found : %s\n", PartitionName));
  return EFI_NOT_FOUND;
}

STATIC VOID FastbootPublishSlotVars (VOID)
{
  UINT32 i;
  UINT32 j;
  CHAR8 *Suffix = NULL;
  UINT32 PartitionCount = 0;
  CHAR8 PartitionNameAscii[MAX_GPT_NAME_SIZE];
  UINT32 RetryCount = 0;
  BOOLEAN Set = FALSE;

  GetPartitionCount (&PartitionCount);
  /*Scan through partition entries, populate the attributes*/
  for (i = 0, j = 0; i < PartitionCount && j < SlotCount; i++) {
    UnicodeStrToAsciiStr (PtnEntries[i].PartEntry.PartitionName,
                          PartitionNameAscii);

    if (!(AsciiStrnCmp (PartitionNameAscii, "boot", AsciiStrLen ("boot")))) {
      Suffix = PartitionNameAscii + AsciiStrLen ("boot_");

      AsciiStrnCpyS (BootSlotInfo[j].SlotSuffix, MAX_SLOT_SUFFIX_SZ, Suffix,
                     AsciiStrLen (Suffix));
      AsciiStrnCpyS (BootSlotInfo[j].SlotSuccessfulVar, SLOT_ATTR_SIZE,
                     "slot-successful:", AsciiStrLen ("slot-successful:"));
      Set = PtnEntries[i].PartEntry.Attributes & PART_ATT_SUCCESSFUL_VAL
                ? TRUE
                : FALSE;
      AsciiStrnCpyS (BootSlotInfo[j].SlotSuccessfulVal, ATTR_RESP_SIZE,
                     Set ? "yes" : "no",
                     Set ? AsciiStrLen ("yes") : AsciiStrLen ("no"));
      AsciiStrnCatS (BootSlotInfo[j].SlotSuccessfulVar, SLOT_ATTR_SIZE, Suffix,
                     AsciiStrLen (Suffix));
      FastbootPublishVar (BootSlotInfo[j].SlotSuccessfulVar,
                          BootSlotInfo[j].SlotSuccessfulVal);

      AsciiStrnCpyS (BootSlotInfo[j].SlotUnbootableVar, SLOT_ATTR_SIZE,
                     "slot-unbootable:", AsciiStrLen ("slot-unbootable:"));
      Set = PtnEntries[i].PartEntry.Attributes & PART_ATT_UNBOOTABLE_VAL
                ? TRUE
                : FALSE;
      AsciiStrnCpyS (BootSlotInfo[j].SlotUnbootableVal, ATTR_RESP_SIZE,
                     Set ? "yes" : "no",
                     Set ? AsciiStrLen ("yes") : AsciiStrLen ("no"));
      AsciiStrnCatS (BootSlotInfo[j].SlotUnbootableVar, SLOT_ATTR_SIZE, Suffix,
                     AsciiStrLen (Suffix));
      FastbootPublishVar (BootSlotInfo[j].SlotUnbootableVar,
                          BootSlotInfo[j].SlotUnbootableVal);

      AsciiStrnCpyS (BootSlotInfo[j].SlotRetryCountVar, SLOT_ATTR_SIZE,
                     "slot-retry-count:", AsciiStrLen ("slot-retry-count:"));
      RetryCount =
          (PtnEntries[i].PartEntry.Attributes & PART_ATT_MAX_RETRY_COUNT_VAL) >>
          PART_ATT_MAX_RETRY_CNT_BIT;
      AsciiSPrint (BootSlotInfo[j].SlotRetryCountVal, ATTR_RESP_SIZE, "%llu",
                   RetryCount);
      AsciiStrnCatS (BootSlotInfo[j].SlotRetryCountVar, SLOT_ATTR_SIZE, Suffix,
                     AsciiStrLen (Suffix));
      FastbootPublishVar (BootSlotInfo[j].SlotRetryCountVar,
                          BootSlotInfo[j].SlotRetryCountVal);
      j++;
    }
  }
  FastbootPublishVar ("has-slot:boot", "yes");
  UnicodeStrToAsciiStr (GetCurrentSlotSuffix ().Suffix, CurrentSlotFB);

  /* Here CurrentSlotFB will only have value of "_a" or "_b".*/
  SKIP_FIRSTCHAR_IN_SLOT_SUFFIX (CurrentSlotFB);

  FastbootPublishVar ("current-slot", CurrentSlotFB);
  FastbootPublishVar ("has-slot:system",
                      PartitionHasMultiSlot ((CONST CHAR16 *)L"system") ? "yes"
                                                                        : "no");
  FastbootPublishVar ("has-slot:modem",
                      PartitionHasMultiSlot ((CONST CHAR16 *)L"modem") ? "yes"
                                                                       : "no");
  return;
}

/*Function to populate attribute fields
 *Note: It traverses through the partition entries structure,
 *populates has-slot, slot-successful,slot-unbootable and
 *slot-retry-count attributes of the boot slots.
 */
STATIC VOID PopulateMultislotMetadata (VOID)
{
  UINT32 i;
  UINT32 j;
  UINT32 PartitionCount = 0;
  CHAR8 *Suffix = NULL;
  CHAR8 PartitionNameAscii[MAX_GPT_NAME_SIZE];

  GetPartitionCount (&PartitionCount);
  if (!InitialPopulate) {
    /*Traverse through partition entries,count matching slots with boot */
    for (i = 0; i < PartitionCount; i++) {
      UnicodeStrToAsciiStr (PtnEntries[i].PartEntry.PartitionName,
                            PartitionNameAscii);
      if (!(AsciiStrnCmp (PartitionNameAscii, "boot", AsciiStrLen ("boot")))) {
        SlotCount++;
        Suffix = PartitionNameAscii + AsciiStrLen ("boot");
        if (!AsciiStrStr (SlotSuffixArray, Suffix)) {
          AsciiStrnCatS (SlotSuffixArray, sizeof (SlotSuffixArray), Suffix,
                         AsciiStrLen (Suffix));
          AsciiStrnCatS (SlotSuffixArray, sizeof (SlotSuffixArray), ",",
                         AsciiStrLen (","));
        }
      }
    }

    AsciiSPrint (SlotCountVar, sizeof (SlotCountVar), "%d", SlotCount);
    FastbootPublishVar ("slot-count", SlotCountVar);

    /*Allocate memory for available number of slots*/
    BootSlotInfo = AllocateZeroPool (
                         SlotCount * sizeof (struct GetVarSlotInfo));
    if (BootSlotInfo == NULL) {
      DEBUG ((EFI_D_ERROR, "Unable to allocate memory for BootSlotInfo\n"));
      return;
    }
    FastbootPublishSlotVars ();
    InitialPopulate = TRUE;
  } else {
    /*While updating gpt from fastboot dont need to populate all the variables
     * as above*/
    for (i = 0; i < SlotCount; i++) {
      AsciiStrnCpyS (BootSlotInfo[i].SlotSuccessfulVal,
                     sizeof (BootSlotInfo[i].SlotSuccessfulVal), "no",
                     AsciiStrLen ("no"));
      AsciiStrnCpyS (BootSlotInfo[i].SlotUnbootableVal,
                     sizeof (BootSlotInfo[i].SlotUnbootableVal), "no",
                     AsciiStrLen ("no"));
      AsciiSPrint (BootSlotInfo[i].SlotRetryCountVal,
                   sizeof (BootSlotInfo[j].SlotRetryCountVal), "%d",
                   MAX_RETRY_COUNT);
    }
  }
  return;
}

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
/* Helper function to write data to disk */
STATIC EFI_STATUS
WriteToDisk (IN EFI_BLOCK_IO_PROTOCOL *BlockIo,
             IN EFI_HANDLE *Handle,
             IN VOID *Image,
             IN UINT64 Size,
             IN UINT64 offset)
{
  return WriteBlockToPartitionNoFlush (BlockIo, Handle, offset, Size, Image);
}

STATIC BOOLEAN
GetPartitionHasSlot (CHAR16 *PartitionName,
                     UINT32 PnameMaxSize,
                     CHAR16 *SlotSuffix,
                     UINT32 SlotSuffixMaxSize)
{
  INT32 Index = INVALID_PTN;
  INT32 SlotIndex = INVALID_PTN;
  BOOLEAN HasSlot = FALSE;
  Slot CurrentSlot;
  CHAR16 Candidate[MAX_GPT_NAME_SIZE];
  UINTN NameLen;

  /* Explicitly suffixed names are already slot-qualified. Do this before
   * probing the table so an absent/misspelled `foo_a` never mutates into
   * `foo_a_a`. Match only terminal suffixes; names like `vendor_boot`
   * and `init_boot` contain `_b` but are not already slot-qualified. */
  NameLen = StrLen (PartitionName);
  if (NameLen >= 2 &&
      (!StrCmp (PartitionName + NameLen - 2, (CONST CHAR16 *)L"_a") ||
       !StrCmp (PartitionName + NameLen - 2, (CONST CHAR16 *)L"_b"))) {
    StrnCpyS (SlotSuffix, SlotSuffixMaxSize,
              (PartitionName + (StrLen (PartitionName) - 2)), 2);
    return TRUE;
  }

  Index = GetPartitionIndex (PartitionName);
  if (Index == INVALID_PTN) {
    CurrentSlot = GetCurrentSlotSuffix ();
    StrnCpyS (Candidate, ARRAY_SIZE (Candidate), PartitionName,
              StrLen (PartitionName));
    StrnCatS (Candidate, ARRAY_SIZE (Candidate), CurrentSlot.Suffix,
              StrLen (CurrentSlot.Suffix));

    SlotIndex = GetPartitionIndex (Candidate);
    if (SlotIndex != INVALID_PTN) {
      StrnCpyS (SlotSuffix, SlotSuffixMaxSize, CurrentSlot.Suffix,
                StrLen (CurrentSlot.Suffix));
      StrnCpyS (PartitionName, PnameMaxSize, Candidate, StrLen (Candidate));
      HasSlot = TRUE;
    }
  } else {
    HasSlot = FALSE;
  }
  return HasSlot;
}

STATIC EFI_STATUS
HandleChunkTypeRaw (sparse_header_t *sparse_header,
        chunk_header_t *chunk_header,
        VOID **Image,
        SparseImgParam *SparseImgData)
{
  EFI_STATUS Status;

  if (sparse_header == NULL ||
      chunk_header == NULL ||
      *Image == NULL ||
      SparseImgData == NULL) {
    DEBUG ((EFI_D_ERROR, "Invalid input Parameters\n"));
    return EFI_INVALID_PARAMETER;
  }

  if ((UINT64)chunk_header->total_sz !=
      ((UINT64)sparse_header->chunk_hdr_sz +
       SparseImgData->ChunkDataSz)) {
    DEBUG ((EFI_D_ERROR, "Bogus chunk size for chunk type Raw\n"));
    return EFI_INVALID_PARAMETER;
  }

  if (CHECK_ADD64 ((UINT64)*Image, SparseImgData->ChunkDataSz)) {
    DEBUG ((EFI_D_ERROR,
            "Integer overflow while adding Image and chunk data sz\n"));
    return EFI_INVALID_PARAMETER;
  }

  if (SparseImgData->ImageEnd < (UINT64)*Image +
      SparseImgData->ChunkDataSz) {
    DEBUG ((EFI_D_ERROR,
            "buffer overreads occured due to invalid sparse header\n"));
    return EFI_INVALID_PARAMETER;
  }

  /* Data is validated, now write to the disk */
  SparseImgData->WrittenBlockCount =
    SparseImgData->TotalBlocks * SparseImgData->BlockCountFactor;
  Status = WriteToDisk (SparseImgData->BlockIo, SparseImgData->Handle,
                        *Image,
                        SparseImgData->ChunkDataSz,
                        SparseImgData->WrittenBlockCount);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Flash Write Failure\n"));
    return Status;
  }

  if (SparseImgData->TotalBlocks >
       (MAX_UINT32 - chunk_header->chunk_sz)) {
    DEBUG ((EFI_D_ERROR, "Bogus size for RAW chunk Type\n"));
    return EFI_INVALID_PARAMETER;
  }

  SparseImgData->TotalBlocks += chunk_header->chunk_sz;
  *Image += SparseImgData->ChunkDataSz;

  return EFI_SUCCESS;
}

STATIC EFI_STATUS
HandleChunkTypeFill (sparse_header_t *sparse_header,
        chunk_header_t *chunk_header,
        VOID **Image,
        SparseImgParam *SparseImgData)
{
  UINT32 *FillBuf = NULL;
  UINT32 FillVal;
  EFI_STATUS Status = EFI_SUCCESS;
  UINT32 Temp;

  if (sparse_header == NULL ||
      chunk_header == NULL ||
      *Image == NULL ||
      SparseImgData == NULL) {
    DEBUG ((EFI_D_ERROR, "Invalid input Parameters\n"));
    return EFI_INVALID_PARAMETER;
  }

  if (chunk_header->total_sz !=
     (sparse_header->chunk_hdr_sz + sizeof (UINT32))) {
    DEBUG ((EFI_D_ERROR, "Bogus chunk size for chunk type FILL\n"));
    return EFI_INVALID_PARAMETER;
  }

  FillBuf = AllocateZeroPool (sparse_header->blk_sz);
  if (!FillBuf) {
    DEBUG ((EFI_D_ERROR, "Malloc failed for: CHUNK_TYPE_FILL\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  if (CHECK_ADD64 ((UINT64)*Image, sizeof (UINT32))) {
    DEBUG ((EFI_D_ERROR,
              "Integer overflow while adding Image and uint32\n"));
    Status = EFI_INVALID_PARAMETER;
    goto out;
  }

  if (SparseImgData->ImageEnd < (UINT64)*Image + sizeof (UINT32)) {
    DEBUG ((EFI_D_ERROR,
            "Buffer overread occured due to invalid sparse header\n"));
   Status = EFI_INVALID_PARAMETER;
   goto out;
  }

  FillVal = *(UINT32 *)*Image;
  *Image = (CHAR8 *)*Image + sizeof (UINT32);

  for (Temp = 0;
       Temp < (sparse_header->blk_sz / sizeof (FillVal));
       Temp++) {
    FillBuf[Temp] = FillVal;
  }

  for (Temp = 0; Temp < chunk_header->chunk_sz; Temp++) {
    /* Make sure the data does not exceed the partition size */
    if ((UINT64)SparseImgData->TotalBlocks *
         (UINT64)sparse_header->blk_sz +
         sparse_header->blk_sz >
         SparseImgData->PartitionSize) {
      DEBUG ((EFI_D_ERROR, "Chunk data size for fill type "
                            "exceeds partition size\n"));
      Status = EFI_VOLUME_FULL;
      goto out;
    }

    SparseImgData->WrittenBlockCount =
      SparseImgData->TotalBlocks *
        SparseImgData->BlockCountFactor;
    Status = WriteToDisk (SparseImgData->BlockIo,
                          SparseImgData->Handle,
                          (VOID *)FillBuf,
                          sparse_header->blk_sz,
                          SparseImgData->WrittenBlockCount);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Flash write failure for FILL Chunk\n"));

    goto out;
    }

    SparseImgData->TotalBlocks++;
  }

  out:
    if (FillBuf) {
    FreePool (FillBuf);
    FillBuf = NULL;
    }
    return Status;
}

STATIC EFI_STATUS
ValidateChunkDataAndFlash (sparse_header_t *sparse_header,
             chunk_header_t *chunk_header,
             VOID **Image,
             SparseImgParam *SparseImgData)
{
  EFI_STATUS Status;

  if (sparse_header == NULL ||
      chunk_header == NULL ||
      *Image == NULL ||
      SparseImgData == NULL) {
    DEBUG ((EFI_D_ERROR, "Invalid input Parameters\n"));
    return EFI_INVALID_PARAMETER;
  }

  switch (chunk_header->chunk_type) {
    case CHUNK_TYPE_RAW:
    Status = HandleChunkTypeRaw (sparse_header,
                                 chunk_header,
                                 Image,
                                 SparseImgData);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    break;

    case CHUNK_TYPE_FILL:
      Status = HandleChunkTypeFill (sparse_header,
                                    chunk_header,
                                    Image,
                                    SparseImgData);

      if (EFI_ERROR (Status)) {
        return Status;
      }

    break;

    case CHUNK_TYPE_DONT_CARE:
      if (SparseImgData->TotalBlocks >
           (MAX_UINT32 - chunk_header->chunk_sz)) {
        DEBUG ((EFI_D_ERROR, "bogus size for chunk DONT CARE type\n"));
        return EFI_INVALID_PARAMETER;
      }
      SparseImgData->TotalBlocks += chunk_header->chunk_sz;
    break;

    case CHUNK_TYPE_CRC:
      if (chunk_header->total_sz != sparse_header->chunk_hdr_sz) {
        DEBUG ((EFI_D_ERROR, "Bogus chunk size for chunk type CRC\n"));
        return EFI_INVALID_PARAMETER;
      }

      if (SparseImgData->TotalBlocks >
           (MAX_UINT32 - chunk_header->chunk_sz)) {
        DEBUG ((EFI_D_ERROR, "Bogus size for chunk type CRC\n"));
        return EFI_INVALID_PARAMETER;
      }

      SparseImgData->TotalBlocks += chunk_header->chunk_sz;


      if (CHECK_ADD64 ((UINT64)*Image, SparseImgData->ChunkDataSz)) {
        DEBUG ((EFI_D_ERROR,
                "Integer overflow while adding Image and chunk data sz\n"));
        return EFI_INVALID_PARAMETER;
      }

      *Image += (UINT32)SparseImgData->ChunkDataSz;
      if (SparseImgData->ImageEnd < (UINT64)*Image) {
        DEBUG ((EFI_D_ERROR, "buffer overreads occured due to "
                              "invalid sparse header\n"));
        return EFI_INVALID_PARAMETER;
      }
    break;

    default:
      DEBUG ((EFI_D_ERROR, "Unknown chunk type: %x\n",
             chunk_header->chunk_type));
      return EFI_INVALID_PARAMETER;
  }
  return EFI_SUCCESS;
}

/* Handle Sparse Image Flashing */
STATIC
EFI_STATUS
HandleSparseImgFlash (IN CHAR16 *PartitionName,
                      IN UINT32 PartitionMaxSize,
                      IN VOID *Image,
                      IN UINT64 sz)
{
  sparse_header_t *sparse_header;
  chunk_header_t *chunk_header;
  EFI_STATUS Status;

  SparseImgParam SparseImgData = {0};

  if (CHECK_ADD64 ((UINT64)Image, sz)) {
    DEBUG ((EFI_D_ERROR, "Integer overflow while adding Image and sz\n"));
    return EFI_INVALID_PARAMETER;
  }

  SparseImgData.ImageEnd = (UINT64)Image + sz;
  /* Caller to ensure that the partition is present in the Partition Table*/
  Status = PartitionGetInfo (PartitionName,
                             &(SparseImgData.BlockIo),
                             &(SparseImgData.Handle));

  if (Status != EFI_SUCCESS)
    return Status;
  if (!SparseImgData.BlockIo) {
    DEBUG ((EFI_D_ERROR, "BlockIo for %a is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }
  if (!SparseImgData.Handle) {
    DEBUG ((EFI_D_ERROR, "EFI handle for %a is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }
  // Check image will fit on device
  SparseImgData.PartitionSize = GetPartitionSize (SparseImgData.BlockIo);
  if (!SparseImgData.PartitionSize) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (sz < sizeof (sparse_header_t)) {
    DEBUG ((EFI_D_ERROR, "Input image is invalid\n"));
    return EFI_INVALID_PARAMETER;
  }

  sparse_header = (sparse_header_t *)Image;
  if (((UINT64)sparse_header->total_blks * (UINT64)sparse_header->blk_sz) >
      SparseImgData.PartitionSize) {
    DEBUG ((EFI_D_ERROR, "Image is too large for the partition\n"));
    return EFI_VOLUME_FULL;
  }

  Image += sizeof (sparse_header_t);

  if (sparse_header->file_hdr_sz != sizeof (sparse_header_t)) {
    DEBUG ((EFI_D_ERROR, "Sparse header size mismatch\n"));
    return EFI_BAD_BUFFER_SIZE;
  }

  if (!sparse_header->blk_sz) {
    DEBUG ((EFI_D_ERROR, "Invalid block size in the sparse header\n"));
    return EFI_INVALID_PARAMETER;
  }

  if ((sparse_header->blk_sz) % (SparseImgData.BlockIo->Media->BlockSize)) {
    DEBUG ((EFI_D_ERROR, "Unsupported sparse block size %x\n",
            sparse_header->blk_sz));
    return EFI_INVALID_PARAMETER;
  }

  SparseImgData.BlockCountFactor = (sparse_header->blk_sz) /
                                   (SparseImgData.BlockIo->Media->BlockSize);

  DEBUG ((EFI_D_VERBOSE, "=== Sparse Image Header ===\n"));
  DEBUG ((EFI_D_VERBOSE, "magic: 0x%x\n", sparse_header->magic));
  DEBUG (
      (EFI_D_VERBOSE, "major_version: 0x%x\n", sparse_header->major_version));
  DEBUG (
      (EFI_D_VERBOSE, "minor_version: 0x%x\n", sparse_header->minor_version));
  DEBUG ((EFI_D_VERBOSE, "file_hdr_sz: %d\n", sparse_header->file_hdr_sz));
  DEBUG ((EFI_D_VERBOSE, "chunk_hdr_sz: %d\n", sparse_header->chunk_hdr_sz));
  DEBUG ((EFI_D_VERBOSE, "blk_sz: %d\n", sparse_header->blk_sz));
  DEBUG ((EFI_D_VERBOSE, "total_blks: %d\n", sparse_header->total_blks));
  DEBUG ((EFI_D_VERBOSE, "total_chunks: %d\n", sparse_header->total_chunks));

  /* Start processing the chunks */
  for (SparseImgData.Chunk = 0;
       SparseImgData.Chunk < sparse_header->total_chunks;
       SparseImgData.Chunk++) {

    if (((UINT64)SparseImgData.TotalBlocks * (UINT64)sparse_header->blk_sz) >=
        SparseImgData.PartitionSize) {
      DEBUG ((EFI_D_ERROR, "Size of image is too large for the partition\n"));
      return EFI_VOLUME_FULL;
    }

    /* Read and skip over chunk header */
    chunk_header = (chunk_header_t *)Image;

    if (CHECK_ADD64 ((UINT64)Image, sizeof (chunk_header_t))) {
      DEBUG ((EFI_D_ERROR,
              "Integer overflow while adding Image and chunk header\n"));
      return EFI_INVALID_PARAMETER;
    }
    Image += sizeof (chunk_header_t);

    if (SparseImgData.ImageEnd < (UINT64)Image) {
      DEBUG ((EFI_D_ERROR,
              "buffer overreads occured due to invalid sparse header\n"));
      return EFI_BAD_BUFFER_SIZE;
    }

    DEBUG ((EFI_D_VERBOSE, "=== Chunk Header ===\n"));
    DEBUG ((EFI_D_VERBOSE, "chunk_type: 0x%x\n", chunk_header->chunk_type));
    DEBUG ((EFI_D_VERBOSE, "chunk_data_sz: 0x%x\n", chunk_header->chunk_sz));
    DEBUG ((EFI_D_VERBOSE, "total_size: 0x%x\n", chunk_header->total_sz));

    if (sparse_header->chunk_hdr_sz != sizeof (chunk_header_t)) {
      DEBUG ((EFI_D_ERROR, "chunk header size mismatch\n"));
      return EFI_INVALID_PARAMETER;
    }

    SparseImgData.ChunkDataSz = (UINT64)sparse_header->blk_sz *
                                 chunk_header->chunk_sz;
    /* Make sure that chunk size calculate from sparse image does not exceed the
     * partition size
     */
    if ((UINT64)SparseImgData.TotalBlocks *
        (UINT64)sparse_header->blk_sz +
        SparseImgData.ChunkDataSz >
        SparseImgData.PartitionSize) {
      DEBUG ((EFI_D_ERROR, "Chunk data size exceeds partition size\n"));
      return EFI_VOLUME_FULL;
    }

    Status = ValidateChunkDataAndFlash (sparse_header,
                                        chunk_header,
                                        &Image,
                                        &SparseImgData);

    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  DEBUG ((EFI_D_INFO, "Wrote %d blocks, expected to write %d blocks\n",
            SparseImgData.TotalBlocks, sparse_header->total_blks));

  if (SparseImgData.TotalBlocks != sparse_header->total_blks) {
    DEBUG ((EFI_D_ERROR, "Sparse Image Write Failure\n"));
    Status = EFI_VOLUME_CORRUPTED;
  } else if (((SparseImgData.BlockIo)->FlushBlocks (SparseImgData.BlockIo))
               != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Sparse Image Flush Failure\n"));
    Status = EFI_DEVICE_ERROR;
  }
  return Status;
}

STATIC VOID
FastbootUpdateAttr (CONST CHAR16 *SlotSuffix)
{
  struct PartitionEntry *Ptn_Entries_Ptr = NULL;
  UINT32 j;
  INT32 Index;
  CHAR16 PartName[MAX_GPT_NAME_SIZE];
  CHAR8 SlotSuffixAscii[MAX_SLOT_SUFFIX_SZ];
  UnicodeStrToAsciiStr (SlotSuffix, SlotSuffixAscii);

  StrnCpyS (PartName, StrLen ((CONST CHAR16 *)L"boot") + 1,
            (CONST CHAR16 *)L"boot", StrLen ((CONST CHAR16 *)L"boot"));
  StrnCatS (PartName, MAX_GPT_NAME_SIZE - 1, SlotSuffix, StrLen (SlotSuffix));

  Index = GetPartitionIndex (PartName);
  if (Index == INVALID_PTN) {
    DEBUG ((EFI_D_ERROR, "Error boot partition for slot: %s not found\n",
            SlotSuffix));
    return;
  }
  Ptn_Entries_Ptr = &PtnEntries[Index];
  Ptn_Entries_Ptr->PartEntry.Attributes &=
      (~PART_ATT_SUCCESSFUL_VAL & ~PART_ATT_UNBOOTABLE_VAL);
  Ptn_Entries_Ptr->PartEntry.Attributes |=
      (PART_ATT_PRIORITY_VAL | PART_ATT_MAX_RETRY_COUNT_VAL);

  UpdatePartitionAttributes (PARTITION_ATTRIBUTES);
  for (j = 0; j < SlotCount; j++) {
    if (AsciiStrStr (SlotSuffixAscii, BootSlotInfo[j].SlotSuffix)) {
      AsciiStrnCpyS (BootSlotInfo[j].SlotSuccessfulVal,
                     sizeof (BootSlotInfo[j].SlotSuccessfulVal), "no",
                     AsciiStrLen ("no"));
      AsciiStrnCpyS (BootSlotInfo[j].SlotUnbootableVal,
                     sizeof (BootSlotInfo[j].SlotUnbootableVal), "no",
                     AsciiStrLen ("no"));
      AsciiSPrint (BootSlotInfo[j].SlotRetryCountVal,
                   sizeof (BootSlotInfo[j].SlotRetryCountVal), "%d",
                   MAX_RETRY_COUNT);
    }
  }
}

#ifdef NAND_UBI_VOLUME_FLASHING_ENABLED
/* UBI Volume flashing */
STATIC
EFI_STATUS
HandleUbiVolFlash (
  IN CHAR16  *VolumeName,
  IN UINT32 VolumeMaxSize,
  IN VOID   *Image,
  IN UINT64   Size)
{
  EFI_STATUS Status = EFI_SUCCESS;
  UINT32 UbiPageSize;
  UINT32 UbiBlockSize;
  EFI_UBI_FLASHER_PROTOCOL *Ubi;
  UBI_FLASHER_HANDLE UbiFlasherHandle;
  CHAR8 VolumeNameAscii[MAX_GPT_NAME_SIZE] = {'\0'};

  Status = gBS->LocateProtocol (&gEfiUbiFlasherProtocolGuid,
                                NULL,
                                (VOID **) &Ubi);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "UBI Volume flashing not supported\n"));
    return Status;
  }

  UnicodeStrToAsciiStr (VolumeName, VolumeNameAscii);
  Status = Ubi->UbiFlasherOpen (VolumeNameAscii,
                                &UbiFlasherHandle,
                                &UbiPageSize,
                                &UbiBlockSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to open UBI volume\n"));
    return Status;
  }

  /* Note: sparse image is not supported for ubi volume flashing */
  Status = Ubi->UbiFlasherWrite (UbiFlasherHandle, 1, Image, Size);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to write UBI volume\n"));
  }

  Status = Ubi->UbiFlasherClose (UbiFlasherHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to close UBI volume\n"));
    return Status;
  }

  return Status;
}
#endif

/* Raw Image flashing */
STATIC
EFI_STATUS
HandleRawImgFlash (IN CHAR16 *PartitionName,
                   IN UINT32 PartitionMaxSize,
                   IN VOID *Image,
                   IN UINT64 Size)
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  UINT64 PartitionSize;
  EFI_HANDLE *Handle = NULL;
  CHAR16 SlotSuffix[MAX_SLOT_SUFFIX_SZ];
  BOOLEAN MultiSlotBoot = PartitionHasMultiSlot ((CONST CHAR16 *)L"boot");
  BOOLEAN HasSlot = FALSE;
#ifdef NAND_UBI_VOLUME_FLASHING_ENABLED
  CHAR16 OrigPartitionName[MAX_GPT_NAME_SIZE];

  /* The MultiSlot logic may not be applicable for all volumes, thus we need
   * to retain the original partition name for volume flashing.
  */
  StrnCpyS (OrigPartitionName, PartitionMaxSize,
                PartitionName, PartitionMaxSize);
#endif
  /* For multislot boot the partition may not support a/b slots.
   * Look for default partition, if it does not exist then try for a/b
   */
  if (MultiSlotBoot)
    HasSlot = GetPartitionHasSlot (PartitionName, PartitionMaxSize, SlotSuffix,
                                   MAX_SLOT_SUFFIX_SZ);

  Status = PartitionGetInfo (PartitionName, &BlockIo, &Handle);
  if (Status != EFI_SUCCESS) {
#ifdef NAND_UBI_VOLUME_FLASHING_ENABLED
    DEBUG ((EFI_D_ERROR, "[%s] Partition Not Found - trying volume\n",
            OrigPartitionName));
    Status = HandleUbiVolFlash (OrigPartitionName,
            ARRAY_SIZE (OrigPartitionName), Image, Size);
#endif
    return Status;
  }
  if (!BlockIo) {
    DEBUG ((EFI_D_ERROR, "BlockIo for %a is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }
  if (!Handle) {
    DEBUG ((EFI_D_ERROR, "EFI handle for %a is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }

  /* Check image will fit on device */
  PartitionSize = GetPartitionSize (BlockIo);
  if (PartitionSize < Size ||
      !PartitionSize) {
    DEBUG ((EFI_D_ERROR, "Partition not big enough.\n"));
    DEBUG ((EFI_D_ERROR, "Partition Size:\t%d\nImage Size:\t%d\n",
            PartitionSize, Size));

    return EFI_VOLUME_FULL;
  }

  Status = WriteBlockToPartition (BlockIo, Handle, 0, Size, Image);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Writing Block to partition Failure\n"));
  }

  if (MultiSlotBoot && HasSlot &&
      !(StrnCmp (PartitionName, (CONST CHAR16 *)L"boot",
                 StrLen ((CONST CHAR16 *)L"boot"))))
    FastbootUpdateAttr (SlotSuffix);

  return Status;
}

/* UBI Image flashing */
STATIC
EFI_STATUS
HandleUbiImgFlash (
  IN CHAR16  *PartitionName,
  IN UINT32 PartitionMaxSize,
  IN VOID   *Image,
  IN UINT64   Size)
{
  EFI_STATUS Status = EFI_SUCCESS;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  UINT32 UbiPageSize;
  UINT32 UbiBlockSize;
  EFI_UBI_FLASHER_PROTOCOL *Ubi;
  UBI_FLASHER_HANDLE UbiFlasherHandle;
  EFI_HANDLE *Handle = NULL;
  CHAR16 SlotSuffix[MAX_SLOT_SUFFIX_SZ];
  BOOLEAN MultiSlotBoot = PartitionHasMultiSlot ((CONST CHAR16 *)L"boot");
  BOOLEAN HasSlot = FALSE;
  CHAR8 PartitionNameAscii[MAX_GPT_NAME_SIZE] = {'\0'};
  UINT64 PartitionSize = 0;

  /* For multislot boot the partition may not support a/b slots.
   * Look for default partition, if it does not exist then try for a/b
   */
  if (MultiSlotBoot) {
    HasSlot =  GetPartitionHasSlot (PartitionName,
                                    PartitionMaxSize,
                                    SlotSuffix,
                                    MAX_SLOT_SUFFIX_SZ);
    DEBUG ((EFI_D_VERBOSE, "Partition has slot=%d\n", HasSlot));
  }

  Status = PartitionGetInfo (PartitionName, &BlockIo, &Handle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Unable to get Parition Info\n"));
    return Status;
  }

  /* Check if Image fits into partition */
  PartitionSize = GetPartitionSize (BlockIo);
  if (Size > PartitionSize ||
    !PartitionSize) {
    DEBUG ((EFI_D_ERROR, "Input Size is invalid\n"));
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->LocateProtocol (&gEfiUbiFlasherProtocolGuid,
                                NULL,
                                (VOID **) &Ubi);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "UBI Image flashing not supported.\n"));
    return Status;
  }

  UnicodeStrToAsciiStr (PartitionName, PartitionNameAscii);
  Status = Ubi->UbiFlasherOpen (PartitionNameAscii,
                                &UbiFlasherHandle,
                                &UbiPageSize,
                                &UbiBlockSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Unable to open UBI Protocol.\n"));
    return Status;
  }

  /* UBI_NUM_IMAGES can replace with number of sparse images being flashed. */
  Status = Ubi->UbiFlasherWrite (UbiFlasherHandle, UBI_NUM_IMAGES, Image, Size);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Unable to open UBI Protocol.\n"));
    return Status;
  }

  Status = Ubi->UbiFlasherClose (UbiFlasherHandle);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Unable to close UBI Protocol.\n"));
    return Status;
  }

  return Status;
}

/* Meta Image flashing */
STATIC
EFI_STATUS
HandleMetaImgFlash (IN CHAR16 *PartitionName,
                    IN UINT32 PartitionMaxSize,
                    IN VOID *Image,
                    IN UINT64 Size)
{
  UINT32 i;
  UINT32 images;
  EFI_STATUS Status = EFI_DEVICE_ERROR;
  img_header_entry_t *img_header_entry;
  meta_header_t *meta_header;
  CHAR16 PartitionNameFromMeta[MAX_GPT_NAME_SIZE];
  UINT64 ImageEnd = 0;
  BOOLEAN PnameTerminated = FALSE;
  UINT32 j;

  if (Size < sizeof (meta_header_t)) {
    DEBUG ((EFI_D_ERROR,
            "Error: The size is smaller than the image header size\n"));
    return EFI_INVALID_PARAMETER;
  }

  meta_header = (meta_header_t *)Image;
  img_header_entry = (img_header_entry_t *)(Image + sizeof (meta_header_t));
  images = meta_header->img_hdr_sz / sizeof (img_header_entry_t);
  if (images > MAX_IMAGES_IN_METAIMG) {
    DEBUG (
        (EFI_D_ERROR,
         "Error: Number of images(%u)in meta_image are greater than expected\n",
         images));
    return EFI_INVALID_PARAMETER;
  }

  if (Size <= (sizeof (meta_header_t) + meta_header->img_hdr_sz)) {
    DEBUG (
        (EFI_D_ERROR,
         "Error: The size is smaller than image header size + entry size\n"));
    return EFI_INVALID_PARAMETER;
  }

  if (CHECK_ADD64 ((UINT64)Image, Size)) {
    DEBUG ((EFI_D_ERROR, "Integer overflow detected in %d, %a\n", __LINE__,
            __FUNCTION__));
    return EFI_BAD_BUFFER_SIZE;
  }
  ImageEnd = (UINT64)Image + Size;

  for (i = 0; i < images; i++) {
    PnameTerminated = FALSE;

    if (!img_header_entry[i].ptn_name[0] ||
        img_header_entry[i].start_offset == 0 || img_header_entry[i].size == 0)
      break;

    if (CHECK_ADD64 ((UINT64)Image, img_header_entry[i].start_offset)) {
      DEBUG ((EFI_D_ERROR, "Integer overflow detected in %d, %a\n", __LINE__,
              __FUNCTION__));
      return EFI_BAD_BUFFER_SIZE;
    }
    if (CHECK_ADD64 ((UINT64) (Image + img_header_entry[i].start_offset),
                     img_header_entry[i].size)) {
      DEBUG ((EFI_D_ERROR, "Integer overflow detected in %d, %a\n", __LINE__,
              __FUNCTION__));
      return EFI_BAD_BUFFER_SIZE;
    }
    if (ImageEnd < ((UINT64)Image + img_header_entry[i].start_offset +
                    img_header_entry[i].size)) {
      DEBUG ((EFI_D_ERROR, "Image size mismatch\n"));
      return EFI_INVALID_PARAMETER;
    }

    for (j = 0; j < MAX_GPT_NAME_SIZE; j++) {
      if (!(img_header_entry[i].ptn_name[j])) {
        PnameTerminated = TRUE;
        break;
      }
    }
    if (!PnameTerminated) {
      DEBUG ((EFI_D_ERROR, "ptn_name string not terminated properly\n"));
      return EFI_INVALID_PARAMETER;
    }
    AsciiStrToUnicodeStr (img_header_entry[i].ptn_name, PartitionNameFromMeta);

    if (!IsUnlockCritical () &&
        IsCriticalPartition (PartitionNameFromMeta)) {
      FastbootFail ("Flashing is not allowed for Critical Partitions\n");
      return EFI_INVALID_PARAMETER;
    }

    Status = HandleRawImgFlash (
        PartitionNameFromMeta, ARRAY_SIZE (PartitionNameFromMeta),
        (void *)Image + img_header_entry[i].start_offset,
        img_header_entry[i].size);
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR, "Meta Image Write Failure\n"));
      return Status;
    }
  }

  Status = UpdateDevInfo (PartitionName, meta_header->img_version);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Unable to Update DevInfo\n"));
  }
  return Status;
}

/* Erase partition */
STATIC EFI_STATUS
FastbootErasePartition (IN CHAR16 *PartitionName)
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_HANDLE *Handle = NULL;

  Status = PartitionGetInfo (PartitionName, &BlockIo, &Handle);
  if (Status != EFI_SUCCESS)
    return Status;
  if (!BlockIo) {
    DEBUG ((EFI_D_ERROR, "BlockIo for %s is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }
  if (!Handle) {
    DEBUG ((EFI_D_ERROR, "EFI handle for %s is corrupted\n", PartitionName));
    return EFI_VOLUME_CORRUPTED;
  }

  Status = ErasePartition (BlockIo, Handle);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Partition Erase failed: %r\n", Status));
    return Status;
  }

  if (!(StrCmp (L"userdata", PartitionName)))
    Status = ResetDeviceState ();

  return Status;
}

INT32 __attribute__ ( (no_sanitize ("safe-stack")))
SparseImgFlashThread (VOID* Arg)
{
  Thread* CurrentThread = KernIntf->Thread->GetCurrentThread ();
  FlashInfo* ThreadFlashInfo = (FlashInfo*) Arg;

  if (!ThreadFlashInfo || !ThreadFlashInfo->FlashDataBuffer) {
    return 0;
  }

  KernIntf->Lock->AcquireLock (LockFlash);
  IsFlashComplete = FALSE;
  FlashSplitNeeded = TRUE;

  HandleSparseImgFlash (ThreadFlashInfo->PartitionName,
          ThreadFlashInfo->PartitionSize,
          ThreadFlashInfo->FlashDataBuffer,
          ThreadFlashInfo->FlashNumDataBytes);

  FlashSplitNeeded = FALSE;
  IsFlashComplete = TRUE;
  KernIntf->Lock->ReleaseLock (LockFlash);

  ThreadStackNodeRemove (CurrentThread);

  FreePool (ThreadFlashInfo);
  ThreadFlashInfo = NULL;

  KernIntf->Thread->ThreadExit (0);

  return 0;
}

EFI_STATUS CreateSparseImgFlashThread (IN FlashInfo* ThreadFlashInfo)
{
  EFI_STATUS Status = EFI_SUCCESS;
  Thread* SparseImgFlashTD = NULL;

  SparseImgFlashTD = KernIntf->Thread->ThreadCreate ("SparseImgFlashThread",
      SparseImgFlashThread, (VOID*)ThreadFlashInfo, UEFI_THREAD_PRIORITY,
      DEFAULT_STACK_SIZE);

  if (SparseImgFlashTD == NULL) {
    return EFI_NOT_READY;
  }

  AllocateUnSafeStackPtr (SparseImgFlashTD);

  Status = KernIntf->Thread->ThreadResume (SparseImgFlashTD);
  return Status;
}

#endif

/* Handle Download Command */
STATIC VOID
CmdDownload (IN CONST CHAR8 *arg, IN VOID *data, IN UINT32 sz)
{
  CHAR8 Response[13] = "DATA";
  UINT32 InitStrLen = AsciiStrLen ("DATA");

  CHAR16 OutputString[FASTBOOT_STRING_MAX_LENGTH];
  CHAR8 *NumBytesString = (CHAR8 *)arg;

  /* Argument is 8-character ASCII string hex representation of number of
   * bytes that will be sent in the data phase.Response is "DATA" + that same
   * 8-character string.
   */

  if (AsciiStrLen (NumBytesString) > ASCII_HEX_STRING_MAX_LENGTH ) {
    DEBUG ((EFI_D_ERROR, "ERROR: Invalid argument size\n"));
    FastbootFail (" Invalid argument size");
    return;
  }

  // Parse out number of data bytes to expect
  mNumDataBytes = AsciiStrHexToUint64 (NumBytesString);
  if (mNumDataBytes == 0) {
    DEBUG (
        (EFI_D_ERROR, "ERROR: Fail to get the number of bytes to download.\n"));
    FastbootFail ("Failed to get the number of bytes to download");
    return;
  }

  if (mNumDataBytes > MaxDownLoadSize) {
    DEBUG ((EFI_D_ERROR,
            "ERROR: Data size (%d) is more than max download size (%d)\n",
            mNumDataBytes, MaxDownLoadSize));
    FastbootFail ("Requested download size is more than max allowed\n");
    return;
  }

  UnicodeSPrint (OutputString, sizeof (OutputString),
                 (CONST CHAR16 *)L"Downloading %d bytes\r\n", mNumDataBytes);

  /* NumBytesString is a 8 bit string, InitStrLen is 4, and the AsciiStrnCpyS()
   * require "DestMax > SourceLen", so init length of Response as 13.
   */
  AsciiStrnCpyS (Response + InitStrLen, sizeof (Response) - InitStrLen,
                 NumBytesString, AsciiStrLen (NumBytesString));

  gBS->CopyMem (GetFastbootDeviceData ()->gTxBuffer, Response,
                sizeof (Response));

  if (IsUseMThreadParallel ()) {
    KernIntf->Lock->AcquireLock (LockDownload);
  }

  mState = ExpectDataState;
  mBytesReceivedSoFar = 0;
  GetFastbootDeviceData ()->UsbDeviceProtocol->Send (
      ENDPOINT_OUT, sizeof (Response), GetFastbootDeviceData ()->gTxBuffer);
  DEBUG ((EFI_D_VERBOSE, "CmdDownload: Send 12 %a\n",
          GetFastbootDeviceData ()->gTxBuffer));
}

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
/*  Function needed for event notification callback */
STATIC VOID
BlockIoCallback (IN EFI_EVENT Event, IN VOID *Context)
{
}

STATIC VOID
UsbTimerHandler (IN EFI_EVENT Event, IN VOID *Context)
{
  HandleUsbEvents ();
  if (FastbootFatal ())
    DEBUG ((EFI_D_ERROR, "Continue detected, Exiting App...\n"));
}

STATIC EFI_STATUS
HandleUsbEventsInTimer ()
{
  EFI_STATUS Status = EFI_SUCCESS;

  if (UsbTimerEvent)
    return Status;

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                             UsbTimerHandler, NULL, &UsbTimerEvent);

  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (UsbTimerEvent, TimerPeriodic, 100000);
  }

  return Status;
}

STATIC VOID StopUsbTimer (VOID)
{
  if (UsbTimerEvent) {
    gBS->SetTimer (UsbTimerEvent, TimerCancel, 0);
    gBS->CloseEvent (UsbTimerEvent);
    UsbTimerEvent = NULL;
  }

  UsbTimerStarted = FALSE;
}
#else
STATIC VOID StopUsbTimer (VOID)
{
  return;
}
#endif

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
STATIC BOOLEAN
NamePropertyMatches (CHAR8 *Name)
{

  return (BOOLEAN) (
      !AsciiStrnCmp (Name, "has-slot", AsciiStrLen ("has-slot")) ||
      !AsciiStrnCmp (Name, "current-slot", AsciiStrLen ("current-slot")) ||
      !AsciiStrnCmp (Name, "slot-retry-count",
                     AsciiStrLen ("slot-retry-count")) ||
      !AsciiStrnCmp (Name, "slot-unbootable",
                     AsciiStrLen ("slot-unbootable")) ||
      !AsciiStrnCmp (Name, "slot-successful",
                     AsciiStrLen ("slot-successful")) ||
      !AsciiStrnCmp (Name, "slot-suffixes", AsciiStrLen ("slot-suffixes")) ||
      !AsciiStrnCmp (Name, "partition-type:system",
                     AsciiStrLen ("partition-type:system")) ||
      !AsciiStrnCmp (Name, "partition-size:system",
                     AsciiStrLen ("partition-size:system")));
}

STATIC VOID ClearFastbootVarsofAB (VOID)
{
  FASTBOOT_VAR *CurrentList = NULL;
  FASTBOOT_VAR *PrevList = NULL;
  FASTBOOT_VAR *NextList = NULL;

  for (CurrentList = Varlist; CurrentList != NULL; CurrentList = NextList) {
    NextList = CurrentList->next;
    if (!NamePropertyMatches ((CHAR8 *)CurrentList->name)) {
      PrevList = CurrentList;
      continue;
    }

    if (!PrevList)
      Varlist = CurrentList->next;
    else
      PrevList->next = CurrentList->next;

    FreePool (CurrentList);
    CurrentList = NULL;
  }
}

VOID
IsBootPtnUpdated (INT32 Lun, BOOLEAN *BootPtnUpdated)
{
  EFI_STATUS Status;
  EFI_PARTITION_ENTRY *PartEntry;
  UINT32 j;

  *BootPtnUpdated = FALSE;
  if (Lun == NO_LUN)
    Lun = 0;

  for (j = 0; j < Ptable[Lun].MaxHandles; j++) {
    Status =
        gBS->HandleProtocol (Ptable[Lun].HandleInfoList[j].Handle,
                             &gEfiPartitionRecordGuid, (VOID **)&PartEntry);

    if (EFI_ERROR (Status)) {
      DEBUG ((
          EFI_D_VERBOSE,
          "Error getting the partition record for Lun %d and Handle: %d : %r\n",
          Lun, j, Status));
      continue;
    }

    if (!StrnCmp (PartEntry->PartitionName, L"boot", StrLen (L"boot"))) {
      DEBUG ((EFI_D_VERBOSE, "Boot Partition is updated\n"));
      *BootPtnUpdated = TRUE;
      return;
    }
  }
}

STATIC BOOLEAN
IsCriticalPartition (CHAR16 *PartitionName)
{
  UINT32 i = 0;

  if (PartitionName == NULL)
    return FALSE;

  for (i = 0; i < ARRAY_SIZE (CriticalPartitions); i++) {
    if (!StrnCmp (PartitionName, CriticalPartitions[i],
                  StrLen (CriticalPartitions[i])))
      return TRUE;
  }

  return FALSE;
}

STATIC BOOLEAN
CheckVirtualAbCriticalPartition (CHAR16 *PartitionName)
{
  VirtualAbMergeStatus SnapshotMergeStatus;
  UINT32 Iter = 0;

  SnapshotMergeStatus = GetSnapshotMergeStatus ();
  if ((SnapshotMergeStatus == MERGING ||
      SnapshotMergeStatus == SNAPSHOTTED)) {
    for (Iter = 0; Iter < ARRAY_SIZE (VirtualAbCriticalPartitions); Iter++) {
      if (!StrnCmp (PartitionName, VirtualAbCriticalPartitions[Iter],
                  StrLen (VirtualAbCriticalPartitions[Iter])))
        return TRUE;
    }
  }

  return FALSE;
}

STATIC VOID ExchangeFlashAndUsbDataBuf (VOID)
{
  VOID *mTmpbuff;

  if (IsUseMThreadParallel ()) {
    KernIntf->Lock->AcquireLock (LockDownload);
    KernIntf->Lock->AcquireLock (LockFlash);
  }

  mTmpbuff = mUsbDataBuffer;
  mUsbDataBuffer = mFlashDataBuffer;
  mFlashDataBuffer = mTmpbuff;
  mFlashNumDataBytes = mNumDataBytes;

  if (IsUseMThreadParallel ()) {
    KernIntf->Lock->ReleaseLock (LockFlash);
    KernIntf->Lock->ReleaseLock (LockDownload);
  }
}

STATIC EFI_STATUS
ReenumeratePartTable (VOID)
{
  EFI_STATUS Status;
  LunSet = FALSE;
  EFI_EVENT gBlockIoRefreshEvt;
  BOOLEAN MultiSlotBoot = FALSE;
  BOOLEAN BootPtnUpdated = FALSE;

  Status =
    gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, BlockIoCallback,
                        NULL, &gBlockIoRefreshGuid, &gBlockIoRefreshEvt);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Error Creating event for Block Io refresh:%x\n",
            Status));
    return Status;
  }

  Status = gBS->SignalEvent (gBlockIoRefreshEvt);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Error Signalling event for Block Io refresh:%x\n",
            Status));
    return Status;
  }
  Status = EnumeratePartitions ();
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Enumeration of partitions failed\n"));
    return Status;
  }
  UpdatePartitionEntries ();

  IsBootPtnUpdated (Lun, &BootPtnUpdated);
  if (BootPtnUpdated) {
    /*Check for multislot boot support*/
    MultiSlotBoot = PartitionHasMultiSlot (L"boot");
    if (MultiSlotBoot) {
      UpdatePartitionAttributes (PARTITION_ALL);
      FindPtnActiveSlot ();
      PopulateMultislotMetadata ();
      DEBUG ((EFI_D_VERBOSE, "Multi Slot boot is supported\n"));
    } else {
      DEBUG ((EFI_D_VERBOSE, "Multi Slot boot is not supported\n"));
      if (BootSlotInfo == NULL) {
        DEBUG ((EFI_D_VERBOSE, "No change in Ptable\n"));
      } else {
        DEBUG ((EFI_D_VERBOSE, "Nullifying A/B info\n"));
        ClearFastbootVarsofAB ();
        FreePool (BootSlotInfo);
        BootSlotInfo = NULL;
        gBS->SetMem ((VOID *)SlotSuffixArray, SLOT_SUFFIX_ARRAY_SIZE, 0);
        InitialPopulate = FALSE;
      }
    }
  }

  DEBUG ((EFI_D_INFO, "*************** New partition Table Dump Start "
                      "*******************\n"));
  PartitionDump ();
  DEBUG ((EFI_D_INFO, "*************** New partition Table Dump End   "
                      "*******************\n"));
  return Status;
}


/* Handle Flash Command */
STATIC VOID
CmdFlash (IN CONST CHAR8 *arg, IN VOID *data, IN UINT32 sz)
{
  EFI_STATUS Status = EFI_SUCCESS;
  sparse_header_t *sparse_header;
  meta_header_t *meta_header;
  UbiHeader_t *UbiHeader;
  CHAR16 PartitionName[MAX_GPT_NAME_SIZE];
  CHAR16 *Token = NULL;
  LunSet = FALSE;
  BOOLEAN MultiSlotBoot = FALSE;
  UINT32 UfsBootLun = 0;
  CHAR8 BootDeviceType[BOOT_DEV_NAME_SIZE_MAX];
  /* For partition info */
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_HANDLE *Handle = NULL;
  BOOLEAN HasSlot = FALSE;
  CHAR16 SlotSuffix[MAX_SLOT_SUFFIX_SZ];
  CHAR8 FlashResultStr[MAX_RSP_SIZE] = "";
  UINT64 PartitionSize = 0;
  UINT32 Ret;
  VirtualAbMergeStatus SnapshotMergeStatus;

  ExchangeFlashAndUsbDataBuf ();
  if (mFlashDataBuffer == NULL) {
    // Doesn't look like we were sent any data
    FastbootFail ("No data to flash");
    return;
  }

  if (AsciiStrLen (arg) >= MAX_GPT_NAME_SIZE) {
    FastbootFail ("Invalid partition name");
    return;
  }
  AsciiStrToUnicodeStr (arg, PartitionName);

  if ((GetAVBVersion () == AVB_LE) ||
      ((GetAVBVersion () != AVB_LE) &&
      (TargetBuildVariantUser ()))) {
    if (!IsUnlocked ()) {
      FastbootFail ("Flashing is not allowed in Lock State");
      return;
    }

    if (!IsUnlockCritical () && IsCriticalPartition (PartitionName)) {
      FastbootFail ("Flashing is not allowed for Critical Partitions\n");
      return;
    }
  }

  if (IsDynamicPartitionSupport ()) {
    /* Virtual A/B is enabled by default.*/
    if (CheckVirtualAbCriticalPartition (PartitionName)) {
      AsciiSPrint (FlashResultStr, MAX_RSP_SIZE,
                    "Flashing of %s is not allowed in %a state",
                    PartitionName, SnapshotMergeState);
      FastbootFail (FlashResultStr);
      return;
    }

    SnapshotMergeStatus = GetSnapshotMergeStatus ();
    if (((SnapshotMergeStatus == MERGING) ||
          (SnapshotMergeStatus == SNAPSHOTTED)) &&
          !StrnCmp (PartitionName, L"super", StrLen (L"super"))) {

      Status = SetSnapshotMergeStatus (CANCELLED);
      if (Status != EFI_SUCCESS) {
        FastbootFail ("Failed to update snapshot state to cancel");
        return;
      }

      //updating fbvar snapshot-merge-state
      AsciiSPrint (SnapshotMergeState,
                    AsciiStrLen (VabSnapshotMergeStatus[NONE_MERGE_STATUS]) + 1,
                    "%a", VabSnapshotMergeStatus[NONE_MERGE_STATUS]);
    }
  }

  /* Handle virtual partition avb_custom_key */
  if (!StrnCmp (PartitionName, L"avb_custom_key", StrLen (L"avb_custom_key"))) {
    DEBUG ((EFI_D_INFO, "flashing avb_custom_key\n"));
    Status = StoreUserKey (data, sz);
    if (Status != EFI_SUCCESS) {
      FastbootFail ("Flashing avb_custom_key failed");
    } else {
      FastbootOkay ("");
    }
    return;
  }

  /* Find the lun number from input string */
  Token = StrStr (PartitionName, L":");

  if (Token) {
    /* Skip past ":" to the lun number */
    Token++;
    Lun = StrDecimalToUintn (Token);

    if (Lun >= MAX_LUNS) {
      FastbootFail ("Invalid Lun number passed\n");
      goto out;
    }

    LunSet = TRUE;
  }

  GetRootDeviceType (BootDeviceType, BOOT_DEV_NAME_SIZE_MAX);

  if ((!StrnCmp (PartitionName, L"partition", StrLen (L"partition"))) ||
       ((!StrnCmp (PartitionName, L"mibib", StrLen (L"mibib"))) &&
       (!AsciiStrnCmp (BootDeviceType, "NAND", AsciiStrLen ("NAND"))))) {
    if (!AsciiStrnCmp (BootDeviceType, "UFS", AsciiStrLen ("UFS"))) {
      UfsGetSetBootLun (&UfsBootLun, TRUE); /* True = Get */
      if (UfsBootLun != 0x1) {
        UfsBootLun = 0x1;
        UfsGetSetBootLun (&UfsBootLun, FALSE); /* False = Set */
      }
    } else if (!AsciiStrnCmp (BootDeviceType, "EMMC", AsciiStrLen ("EMMC")) ||
               !AsciiStrnCmp (BootDeviceType, "NVME", AsciiStrLen ("NVME"))) {
      Lun = NO_LUN;
      LunSet = FALSE;
    }
    DEBUG ((EFI_D_INFO, "Attemping to update partition table\n"));
    DEBUG ((EFI_D_INFO, "*************** Current partition Table Dump Start "
                        "*******************\n"));
    PartitionDump ();
    DEBUG ((EFI_D_INFO, "*************** Current partition Table Dump End   "
                        "*******************\n"));
    if (!AsciiStrnCmp (BootDeviceType, "NAND", AsciiStrLen ("NAND"))) {
      Ret = PartitionVerifyMibibImage (mFlashDataBuffer);
      if (Ret) {
        FastbootFail ("Error Updating partition Table\n");
        goto out;
      }
      Status = HandleRawImgFlash (PartitionName,
                        ARRAY_SIZE (PartitionName),
                        mFlashDataBuffer, mFlashNumDataBytes);
    }
    else {
      Status = UpdatePartitionTable (mFlashDataBuffer, mFlashNumDataBytes,
                        Lun, Ptable);
    }
    /* Signal the Block IO to update and reenumerate the parition table */
    if (Status == EFI_SUCCESS)  {
      Status = ReenumeratePartTable ();
      if (Status == EFI_SUCCESS) {
        FastbootOkay ("");
        goto out;
      }
    }
    FastbootFail ("Error Updating partition Table\n");
    goto out;
  }

  sparse_header = (sparse_header_t *)mFlashDataBuffer;
  meta_header = (meta_header_t *)mFlashDataBuffer;
  UbiHeader = (UbiHeader_t *)mFlashDataBuffer;

  /* Send okay for next data sending */
  if (sparse_header->magic == SPARSE_HEADER_MAGIC) {

    MultiSlotBoot = PartitionHasMultiSlot ((CONST CHAR16 *)L"boot");
    if (MultiSlotBoot) {
      HasSlot = GetPartitionHasSlot (PartitionName,
                                     ARRAY_SIZE (PartitionName),
                                     SlotSuffix, MAX_SLOT_SUFFIX_SZ);
      if (HasSlot) {
        DEBUG ((EFI_D_VERBOSE, "Partition %s has slot\n", PartitionName));
      }
    }

    Status = PartitionGetInfo (PartitionName, &BlockIo, &Handle);
    if (EFI_ERROR (Status)) {
      FastbootFail ("Partition not found");
      goto out;
    }

    PartitionSize = GetPartitionSize (BlockIo);
    if (!PartitionSize) {
      FastbootFail ("Partition error size");
      goto out;
    }

    if ((PartitionSize > MaxDownLoadSize) &&
         !IsDisableParallelDownloadFlash ()) {
      if (IsUseMThreadParallel ()) {
        FlashInfo* ThreadFlashInfo = AllocateZeroPool (sizeof (FlashInfo));
        if (!ThreadFlashInfo) {
          DEBUG ((EFI_D_ERROR,
                  "ERROR: Failed to allocate memory for ThreadFlashInfo\n"));
          return ;
        }

        ThreadFlashInfo->FlashDataBuffer = mFlashDataBuffer,
        ThreadFlashInfo->FlashNumDataBytes = mFlashNumDataBytes;

        StrnCpyS (ThreadFlashInfo->PartitionName, MAX_GPT_NAME_SIZE,
                PartitionName, ARRAY_SIZE (PartitionName));
        ThreadFlashInfo->PartitionSize = ARRAY_SIZE (PartitionName);

        Status = CreateSparseImgFlashThread (ThreadFlashInfo);
      } else {
        IsFlashComplete = FALSE;

        Status = HandleUsbEventsInTimer ();
        if (EFI_ERROR (Status)) {
          DEBUG ((EFI_D_ERROR, "Failed to handle usb event: %r\n", Status));
          IsFlashComplete = TRUE;
          StopUsbTimer ();
        } else {
          UsbTimerStarted = TRUE;
        }
      }

      if (!EFI_ERROR (Status)) {
        FastbootOkay ("");
      }
    }

    if (EFI_ERROR (Status) ||
      !IsUseMThreadParallel () ||
      (PartitionSize <= MaxDownLoadSize)) {
      FlashResult = HandleSparseImgFlash (PartitionName,
                                        ARRAY_SIZE (PartitionName),
                                        mFlashDataBuffer, mFlashNumDataBytes);
      IsFlashComplete = TRUE;
      StopUsbTimer ();
    }
  } else if (!AsciiStrnCmp (UbiHeader->HdrMagic, UBI_HEADER_MAGIC, 4)) {
    FlashResult = HandleUbiImgFlash (PartitionName,
                                     ARRAY_SIZE (PartitionName),
                                     mFlashDataBuffer,
                                     mFlashNumDataBytes);
  } else if (meta_header->magic == META_HEADER_MAGIC) {

    FlashResult = HandleMetaImgFlash (PartitionName,
                                      ARRAY_SIZE (PartitionName),
                                      mFlashDataBuffer, mFlashNumDataBytes);
  } else {

    FlashResult = HandleRawImgFlash (PartitionName,
                                     ARRAY_SIZE (PartitionName),
                                     mFlashDataBuffer, mFlashNumDataBytes);
  }

  /*
   * For Non-sparse image: Check flash result and update the result
   * Also, Handle if there is Failure in handling USB events especially for
   * sparse images.
   */
  if ((sparse_header->magic != SPARSE_HEADER_MAGIC) ||
        (PartitionSize < MaxDownLoadSize) ||
        ((PartitionSize > MaxDownLoadSize) &&
        (IsDisableParallelDownloadFlash () ||
        (Status != EFI_SUCCESS)))) {
    if (EFI_ERROR (FlashResult)) {
      if (FlashResult == EFI_NOT_FOUND) {
        AsciiSPrint (FlashResultStr, MAX_RSP_SIZE, "(%s) No such partition",
                     PartitionName);
      } else {
        AsciiSPrint (FlashResultStr, MAX_RSP_SIZE, "%a : %r",
                     "Error flashing partition", FlashResult);
      }

      DEBUG ((EFI_D_ERROR, "%a\n", FlashResultStr));
      FastbootFail (FlashResultStr);

      /* Reset the Flash Result for next flash command */
      FlashResult = EFI_SUCCESS;
      goto out;
    } else {
      DEBUG ((EFI_D_INFO, "flash image status:  %r\n", FlashResult));
      FastbootOkay ("");
    }
  }

out:
  if ((!AsciiStrnCmp (arg, "system", AsciiStrLen ("system"))||
    !AsciiStrnCmp (arg, "super", AsciiStrLen ("super"))) &&
    !IsEnforcing () &&
    (FlashResult == EFI_SUCCESS)) {
     // reset dm_verity mode to enforcing
    Status = EnableEnforcingMode (TRUE);
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR, "failed to update verity mode:  %r\n", Status));
    }
  }

  LunSet = FALSE;
}

STATIC VOID
CmdErase (IN CONST CHAR8 *arg, IN VOID *data, IN UINT32 sz)
{
  EFI_STATUS Status;
  CHAR16 OutputString[FASTBOOT_STRING_MAX_LENGTH];
  BOOLEAN HasSlot = FALSE;
  CHAR16 SlotSuffix[MAX_SLOT_SUFFIX_SZ];
  BOOLEAN MultiSlotBoot = PartitionHasMultiSlot (L"boot");
  CHAR16 PartitionName[MAX_GPT_NAME_SIZE];
  CHAR8 EraseResultStr[MAX_RSP_SIZE] = "";
  VirtualAbMergeStatus SnapshotMergeStatus;

  WaitForFlashFinished ();

  if (AsciiStrLen (arg) >= MAX_GPT_NAME_SIZE) {
    FastbootFail ("Invalid partition name");
    return;
  }
  AsciiStrToUnicodeStr (arg, PartitionName);


  if ((GetAVBVersion () == AVB_LE) ||
      ((GetAVBVersion () != AVB_LE) &&
      (TargetBuildVariantUser ()))) {
    if (!IsUnlocked ()) {
      FastbootFail ("Erase is not allowed in Lock State");
      return;
    }

    if (!IsUnlockCritical () && IsCriticalPartition (PartitionName)) {
      FastbootFail ("Erase is not allowed for Critical Partitions\n");
      return;
    }
  }

  if (IsDynamicPartitionSupport ()) {
    /* Virtual A/B feature is enabled by default. */
    if (CheckVirtualAbCriticalPartition (PartitionName)) {
      AsciiSPrint (EraseResultStr, MAX_RSP_SIZE,
                    "Erase of %s is not allowed in %a state",
                    PartitionName, SnapshotMergeState);
      FastbootFail (EraseResultStr);
      return;
    }

    SnapshotMergeStatus = GetSnapshotMergeStatus ();
    if (((SnapshotMergeStatus == MERGING) ||
          (SnapshotMergeStatus == SNAPSHOTTED)) &&
          !StrnCmp (PartitionName, L"super", StrLen (L"super"))) {

      Status = SetSnapshotMergeStatus (CANCELLED);
      if (Status != EFI_SUCCESS) {
        FastbootFail ("Failed to update snapshot state to cancel");
        return;
      }

      //updating fbvar snapshot-merge-state
      AsciiSPrint (SnapshotMergeState,
                    AsciiStrLen (VabSnapshotMergeStatus[NONE_MERGE_STATUS]) + 1,
                    "%a", VabSnapshotMergeStatus[NONE_MERGE_STATUS]);
    }
  }

  /* Handle virtual partition avb_custom_key */
  if (!StrnCmp (PartitionName, L"avb_custom_key", StrLen (L"avb_custom_key"))) {
    DEBUG ((EFI_D_INFO, "erasing avb_custom_key\n"));
    Status = EraseUserKey ();
    if (Status != EFI_SUCCESS) {
      FastbootFail ("Erasing avb_custom_key failed");
    } else {
      FastbootOkay ("");
    }
    return;
  }

  /* In A/B to have backward compatibility user can still give fastboot flash
   * boot/system/modem etc
   * based on current slot Suffix try to look for "partition"_a/b if not found
   * fall back to look for
   * just the "partition" in case some of the partitions are no included for A/B
   * implementation
   */
  if (MultiSlotBoot)
    HasSlot = GetPartitionHasSlot (PartitionName, ARRAY_SIZE (PartitionName),
                                   SlotSuffix, MAX_SLOT_SUFFIX_SZ);

  // Build output string
  UnicodeSPrint (OutputString, sizeof (OutputString),
                 L"Erasing partition %s\r\n", PartitionName);
  Status = FastbootErasePartition (PartitionName);
  if (EFI_ERROR (Status)) {
    FastbootFail ("Check device console.");
    DEBUG ((EFI_D_ERROR, "Couldn't erase image:  %r\n", Status));
  } else {
    if (MultiSlotBoot && HasSlot &&
        !(StrnCmp (PartitionName, L"boot", StrLen (L"boot"))))
      FastbootUpdateAttr (SlotSuffix);
    FastbootOkay ("");
  }
}

/*Function to set given slot as high priority
 *Arg: slot Suffix
 *Note: increase the priority of slot to max priority
 *at the same time decrease the priority of other
 *slots.
 */
VOID
CmdSetActive (CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
  CHAR16 SetActive[MAX_GPT_NAME_SIZE] = L"boot";
  CHAR8 *InputSlot = NULL;
  CHAR16 InputSlotInUnicode[MAX_SLOT_SUFFIX_SZ];
  CHAR16 InputSlotInUnicodetemp[MAX_SLOT_SUFFIX_SZ];
  CONST CHAR8 *Delim = ":";
  UINT16 j = 0;
  BOOLEAN SlotVarUpdateComplete = FALSE;
  UINT32 SlotEnd = 0;
  BOOLEAN MultiSlotBoot = PartitionHasMultiSlot (L"boot");
  Slot NewSlot = {{0}};
  EFI_STATUS Status;

  if (TargetBuildVariantUser () && !IsUnlocked ()) {
    FastbootFail ("Slot Change is not allowed in Lock State\n");
    return;
  }

  if (!MultiSlotBoot) {
    FastbootFail ("This Command not supported");
    return;
  }

  if (!Arg) {
    FastbootFail ("Invalid Input Parameters");
    return;
  }

  if (IsDynamicPartitionSupport ()) {
    /* Virtual A/B feature is enabled by default.*/
    if (GetSnapshotMergeStatus () == MERGING) {
      FastbootFail ("Slot Change is not allowed in merging state");
      return;
    }
  }

  InputSlot = AsciiStrStr (Arg, Delim);
  if (InputSlot) {
    InputSlot++;
    if (AsciiStrLen (InputSlot) >= MAX_SLOT_SUFFIX_SZ) {
      FastbootFail ("Invalid Slot");
      return;
    }
    if (!AsciiStrStr (InputSlot, "_")) {
      AsciiStrToUnicodeStr (InputSlot, InputSlotInUnicodetemp);
      StrnCpyS (InputSlotInUnicode, MAX_SLOT_SUFFIX_SZ, L"_", StrLen (L"_"));
      StrnCatS (InputSlotInUnicode, MAX_SLOT_SUFFIX_SZ, InputSlotInUnicodetemp,
                StrLen (InputSlotInUnicodetemp));
    } else {
      AsciiStrToUnicodeStr (InputSlot, InputSlotInUnicode);
    }

    if ((AsciiStrLen (InputSlot) == MAX_SLOT_SUFFIX_SZ - 2) ||
        (AsciiStrLen (InputSlot) == MAX_SLOT_SUFFIX_SZ - 1)) {
      SlotEnd = AsciiStrLen (InputSlot);
      if ((InputSlot[SlotEnd] != '\0') ||
          !AsciiStrStr (SlotSuffixArray, InputSlot)) {
        DEBUG ((EFI_D_ERROR, "%a Invalid InputSlot Suffix\n", InputSlot));
        FastbootFail ("Invalid Slot Suffix");
        return;
      }
    }
    /*Arg will be either _a or _b, so apppend it to boot*/
    StrnCatS (SetActive, MAX_GPT_NAME_SIZE - 1, InputSlotInUnicode,
              StrLen (InputSlotInUnicode));
  } else {
    FastbootFail ("set_active _a or _b should be entered");
    return;
  }

  StrnCpyS (NewSlot.Suffix, ARRAY_SIZE (NewSlot.Suffix), InputSlotInUnicode,
            StrLen (InputSlotInUnicode));
  Status = SetActiveSlot (&NewSlot, TRUE);
  if (Status != EFI_SUCCESS) {
    FastbootFail ("set_active failed");
    return;
  }

  // Updating fbvar `current-slot'
  UnicodeStrToAsciiStr (GetCurrentSlotSuffix ().Suffix, CurrentSlotFB);

  /* Here CurrentSlotFB will only have value of "_a" or "_b".*/
  SKIP_FIRSTCHAR_IN_SLOT_SUFFIX (CurrentSlotFB);

  do {
    if (AsciiStrStr (BootSlotInfo[j].SlotSuffix, InputSlot)) {
      AsciiStrnCpyS (BootSlotInfo[j].SlotSuccessfulVal, ATTR_RESP_SIZE, "no",
                     AsciiStrLen ("no"));
      AsciiStrnCpyS (BootSlotInfo[j].SlotUnbootableVal, ATTR_RESP_SIZE, "no",
                     AsciiStrLen ("no"));
      AsciiSPrint (BootSlotInfo[j].SlotRetryCountVal,
                   sizeof (BootSlotInfo[j].SlotRetryCountVal), "%d",
                   MAX_RETRY_COUNT);
      SlotVarUpdateComplete = TRUE;
    }
    j++;
  } while (!SlotVarUpdateComplete);

  UpdatePartitionAttributes (PARTITION_ALL);
  FastbootOkay ("");
}
#endif

STATIC VOID
FlashCompleteHandler (IN EFI_EVENT Event, IN VOID *Context)
{
  EFI_STATUS Status = EFI_SUCCESS;

  /* Wait for flash completely before sending okay */
  if (!IsFlashComplete) {
    Status = gBS->SetTimer (Event, TimerRelative, 100000);
    if (EFI_ERROR (Status)) {
      FastbootFail ("Failed to set timer for waiting flash completely");
      goto Out;
    }
    return;
  }

  FastbootOkay ("");
Out:
  gBS->CloseEvent (Event);
  Event = NULL;
}

/* Parallel usb sending data and device writing data
 * It's need to delay to send okay until flashing finished for
 * next command.
 */
STATIC EFI_STATUS FastbootOkayDelay (VOID)
{
  EFI_STATUS Status = EFI_SUCCESS;
  EFI_EVENT FlashCompleteEvent = NULL;

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                             FlashCompleteHandler, NULL, &FlashCompleteEvent);
  if (EFI_ERROR (Status)) {
    FastbootFail ("Failed to creat event for waiting flash completely");
    return Status;
  }

  Status = gBS->SetTimer (FlashCompleteEvent, TimerRelative, 100000);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (FlashCompleteEvent);
    FlashCompleteEvent = NULL;
    FastbootFail ("Failed to set timer for waiting flash completely");
  }

  return Status;
}

STATIC VOID
AcceptData (IN UINT64 Size, IN VOID *Data)
{
  UINT64 RemainingBytes = mNumDataBytes - mBytesReceivedSoFar;
  UINT32 PageSize = 0;
  UINT32 RoundSize = 0;

  /* Protocol doesn't say anything about sending extra data so just ignore it.*/
  if (Size > RemainingBytes) {
    Size = RemainingBytes;
  }

  mBytesReceivedSoFar += Size;

  /* Either queue the max transfer size 1 MB or only queue the remaining
   * amount of data left to avoid zlt issues
   */
  if (mBytesReceivedSoFar == mNumDataBytes) {
    /* Download Finished */
    DEBUG ((EFI_D_INFO, "Download Finished\n"));
    /* Zero initialized the surplus data buffer. It's risky to access the data
     * buffer which it's not zero initialized, its content might leak
     */
    GetPageSize (&PageSize);
    RoundSize = ROUND_TO_PAGE (mNumDataBytes, PageSize - 1);
    if (RoundSize < MaxDownLoadSize) {
      gBS->SetMem ((VOID *)(Data + mNumDataBytes), RoundSize - mNumDataBytes,
                   0);
    }
    mState = ExpectCmdState;

    if (IsUseMThreadParallel ())  {
      KernIntf->Lock->ReleaseLock (LockDownload);
      FastbootOkay ("");
    } else {
      /* Stop usb timer after data transfer completed */
      StopUsbTimer ();
      /* Postpone Fastboot Okay until flash completed */
      FastbootOkayDelay ();
    }
  } else {
    GetFastbootDeviceData ()->UsbDeviceProtocol->Send (
        ENDPOINT_IN, GetXfrSize (), (Data + mBytesReceivedSoFar));
    DEBUG ((EFI_D_VERBOSE, "AcceptData: Send %d\n", GetXfrSize ()));
  }
}

/* Called based on the event received from USB device protocol:
 */
VOID
DataReady (IN UINT64 Size, IN VOID *Data)
{
  DEBUG ((EFI_D_VERBOSE, "DataReady %d\n", Size));
  if (mState == ExpectCmdState)
    AcceptCmd (Size, (CHAR8 *)Data);
  else if (mState == ExpectDataState)
    AcceptData (Size, Data);
  else {
    DEBUG ((EFI_D_ERROR, "DataReady Unknown status received\r\n"));
    return;
  }
}

STATIC VOID
FatalErrorNotify (IN EFI_EVENT Event, IN VOID *Context)
{
  DEBUG ((EFI_D_ERROR, "Fatal error sending command response. Exiting.\r\n"));
  Finished = TRUE;
}

/* Fatal error during fastboot */
BOOLEAN FastbootFatal (VOID)
{
  return Finished;
}

/* This function must be called to deallocate the USB buffers, as well
 * as the main Fastboot Buffer. Also Frees Variable data Structure
 */
EFI_STATUS
FastbootCmdsUnInit (VOID)
{
  EFI_STATUS Status;

  if (mDataBuffer) {
    Status = GetFastbootDeviceData ()->UsbDeviceProtocol->FreeTransferBuffer (
        (VOID *)mDataBuffer);
    if (Status != EFI_SUCCESS) {
      DEBUG ((EFI_D_ERROR, "Failed to free up fastboot buffer\n"));
      return Status;
    }
  }
  FastbootUnInit ();
  GetFastbootDeviceData ()->UsbDeviceProtocol->Stop ();
  return EFI_SUCCESS;
}

/* This function must be called to check maximum allocatable chunk for
 * Fastboot Buffer.
 */
STATIC VOID
GetMaxAllocatableMemory (
  OUT UINT64 *FreeSize
  )
{
  EFI_MEMORY_DESCRIPTOR       *MemMap;
  EFI_MEMORY_DESCRIPTOR       *MemMapPtr;
  UINTN                       MemMapSize;
  UINTN                       MapKey, DescriptorSize;
  UINTN                       Index;
  UINTN                       MaxFree = 0;
  UINT32                      DescriptorVersion;
  EFI_STATUS                  Status;

  MemMapSize = 0;
  MemMap     = NULL;
  *FreeSize = 0;

  // Get size of current memory map.
  Status = gBS->GetMemoryMap (&MemMapSize, MemMap, &MapKey,
                              &DescriptorSize, &DescriptorVersion);
  /*
    If the MemoryMap buffer is too small, the EFI_BUFFER_TOO_SMALL error
    code is returned and the MemoryMapSize value contains the size of
    the buffer needed to contain the current memory map.
    The actual size of the buffer allocated for the consequent call
    to GetMemoryMap() should be bigger then the value returned in
    MemMapSize, since allocation of the new buffer may
    potentially increase memory map size.
  */
  if (Status != EFI_BUFFER_TOO_SMALL) {
    DEBUG ((EFI_D_ERROR, "ERROR: Undefined response get memory map\n"));
    return;
  }

  /*
    Allocate some additional memory as returned by MemMapSize,
    and query current memory map.
  */
  if (CHECK_ADD64 (MemMapSize, EFI_PAGE_SIZE)) {
    DEBUG ((EFI_D_ERROR, "ERROR: integer Overflow while adding additional"
                         "memory to MemMapSize"));
    return;
  }
  MemMapSize = MemMapSize + EFI_PAGE_SIZE;
  MemMap = AllocateZeroPool (MemMapSize);
  if (!MemMap) {
    DEBUG ((EFI_D_ERROR,
                    "ERROR: Failed to allocate memory for memory map\n"));
    return;
  }

  // Store pointer to be freed later.
  MemMapPtr = MemMap;
  // Get System MemoryMap
  Status = gBS->GetMemoryMap (&MemMapSize, MemMap, &MapKey,
                              &DescriptorSize, &DescriptorVersion);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "ERROR: Failed to query memory map\n"));
    FreePool (MemMapPtr);
    return;
  }

  // Find largest free chunk of unallocated memory available.
  for (Index = 0; Index < MemMapSize / DescriptorSize; Index ++) {
    if (MemMap->Type == EfiConventionalMemory &&
          MaxFree < MemMap->NumberOfPages) {
          MaxFree = MemMap->NumberOfPages;
    }
    MemMap = (EFI_MEMORY_DESCRIPTOR *)((UINTN)MemMap + DescriptorSize);
  }

  *FreeSize = EFI_PAGES_TO_SIZE (MaxFree);
  DEBUG ((EFI_D_VERBOSE, "Free Memory available: %ld\n", *FreeSize));
  FreePool (MemMapPtr);
  return;
}

//Shoud block command until flash finished
VOID WaitForFlashFinished (VOID)
{
  if (!IsFlashComplete &&
    IsUseMThreadParallel ()) {
    KernIntf->Lock->AcquireLock (LockFlash);
    KernIntf->Lock->ReleaseLock (LockFlash);
  }
}

VOID ThreadSleep (TimeDuration Delay)
{
  KernIntf->Thread->ThreadSleep (Delay);
}

BOOLEAN IsUseMThreadParallel (VOID)
{
  if (FixedPcdGetBool (EnableMultiThreadFlash)) {
    return IsMultiThreadSupported;
  }

  return FALSE;
}

VOID InitMultiThreadEnv ()
{
  EFI_STATUS Status = EFI_SUCCESS;

  if (IsDisableParallelDownloadFlash ()) {
    return;
  }

  Status = gBS->LocateProtocol (&gEfiKernelProtocolGuid, NULL,
      (VOID **)&KernIntf);

  if ((Status != EFI_SUCCESS) ||
    (KernIntf == NULL) ||
    KernIntf->Version < EFI_KERNEL_PROTOCOL_VER_LOCK_API) {
    DEBUG ((EFI_D_VERBOSE, "Multi thread is not supported.\n"));
    return;
  }

  KernIntf->Lock->InitLock ("DOWNLOAD", &LockDownload);
  if (&LockDownload == NULL) {
     DEBUG ((EFI_D_ERROR, "InitLock LockDownload error \n"));
     return;
  }

  KernIntf->Lock->InitLock ("FLASH", &LockFlash);
  if (&LockFlash == NULL) {
    DEBUG ((EFI_D_ERROR, "InitLock LockFlash error \n"));
    KernIntf->Lock->DestroyLock (LockDownload);
    return;
  }

  //init MultiThreadEnv succeeded, use multi thread to flash
  IsMultiThreadSupported = TRUE;

  DEBUG ((EFI_D_VERBOSE,
          "InitMultiThreadEnv successfully, will use thread to flash \n"));
}

STATIC VOID GetBufferSize (UINT64 *MaxBufferSize, UINT64 *MinBufferSize)
{
  EFI_STATUS Status;
  UINT64 DdrSize = 0;

  Status = GetDdrSize (&DdrSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Error getting DDR Type %r\n", Status));
    return;
  }

  if (DdrSize <= DDR_512MB) {
    /* 35MB */
    *MaxBufferSize = 36700160;
    /* 16MB */
    *MinBufferSize = 16777216;
  }
}

/* ---- QcomWDog (Qualcomm hardware watchdog) helpers ---------------------
 *
 * EFIQcomWDog.h isn't carried in this fork's headers - the protocol is
 * exposed at runtime by the XBL-loaded DXE driver. Layout matches
 * QcomSdkPkg/Include/Protocol/EFIQcomWDog.h. Used by FastbootCmdsInit
 * to silence both the QcomWDog itself AND OnePlus Phoenix's periodic
 * re-arming of it (Phoenix is OEM code, no source visibility, so we
 * defensively pet from our own 5s timer rather than trust Disable). */
typedef EFI_STATUS (EFIAPI *EFI_QCOM_WDOG_ENABLE_FN) (VOID);
typedef EFI_STATUS (EFIAPI *EFI_QCOM_WDOG_DISABLE_FN) (VOID);
typedef EFI_STATUS (EFIAPI *EFI_QCOM_WDOG_SET_BITE_TIMEOUT_FN) (
                              IN UINT32 TimeOutSec);
typedef VOID       (EFIAPI *EFI_QCOM_WDOG_FORCE_PET_FN) (VOID);
typedef EFI_STATUS (EFIAPI *EFI_QCOM_WDOG_FORCE_BITE_FN) (VOID);
typedef EFI_STATUS (EFIAPI *EFI_QCOM_WDOG_SET_PET_PERIOD_FN) (
                              IN UINT32 PeroidSec);

typedef struct {
  UINT64                              Version;
  EFI_QCOM_WDOG_ENABLE_FN             Enable;
  EFI_QCOM_WDOG_DISABLE_FN            Disable;
  EFI_QCOM_WDOG_SET_BITE_TIMEOUT_FN   SetBiteTimeout;
  EFI_QCOM_WDOG_FORCE_PET_FN          ForceWDogPet;
  EFI_QCOM_WDOG_FORCE_BITE_FN         ForceWDogBite;
  EFI_QCOM_WDOG_SET_PET_PERIOD_FN     SetPetTimerPeriod;
} EFI_QCOM_WATCHDOG_PROTOCOL;

STATIC EFI_GUID                    gQcomWdogGuid = {
  0x6f8b0fa0, 0x034f, 0x47a4,
  { 0x8c, 0x7a, 0xbc, 0xec, 0x55, 0xb4, 0x1c, 0x64 }
};

/* Reachable from the timer callback; latched by FastbootCmdsInit. */
STATIC EFI_QCOM_WATCHDOG_PROTOCOL *gFastbootWdog     = NULL;
STATIC EFI_EVENT                   gFastbootWdogPet  = NULL;

/* Original gBS->SetWatchdogTimer pointer + our clamp. Installed by
 * FastbootCmdsInit so Phoenix's "Phoenix:Set uefi watchdog time is 60"
 * call (and any other re-arm from OEM threads) becomes a no-op for the
 * timeout dimension while still letting the boot-services table report
 * EFI_SUCCESS. Without this, Phoenix sets a 60s timeout AFTER our
 * one-shot disable and the device resets out of fastboot at 60s. */
typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER_FN) (
                              IN UINTN     Timeout,
                              IN UINT64    WatchdogCode,
                              IN UINTN     DataSize,
                              IN CHAR16   *WatchdogData OPTIONAL);

STATIC EFI_SET_WATCHDOG_TIMER_FN gOriginalSetWatchdogTimer = NULL;

STATIC EFI_STATUS EFIAPI
FastbootWdogTimerClamp (
  IN UINTN    Timeout,
  IN UINT64   WatchdogCode,
  IN UINTN    DataSize,
  IN CHAR16  *WatchdogData OPTIONAL
  )
{
  /* Force timeout=0 (= disabled) regardless of caller. Drop the
   * WatchdogData payload too — non-zero callers tend to pass strings
   * we don't care about. */
  if (gOriginalSetWatchdogTimer != NULL) {
    return gOriginalSetWatchdogTimer (0, WatchdogCode, 0, NULL);
  }
  return EFI_SUCCESS;
}

/* Timer-event callback: pets the QcomWDog every 5s. */
STATIC VOID EFIAPI
FastbootWdogPetCallback (
  IN EFI_EVENT  Event,
  IN VOID      *Context
  )
{
  if (gFastbootWdog != NULL && gFastbootWdog->ForceWDogPet != NULL) {
    gFastbootWdog->ForceWDogPet ();
  }
}

EFI_STATUS
FastbootCmdsInit (VOID)
{
  EFI_STATUS Status;
  EFI_EVENT mFatalSendErrorEvent;
  CHAR8 *FastBootBuffer;
  UINT64 MaxBufferSize = MAX_BUFFER_SIZE;
  UINT64 MinBufferSize = MIN_BUFFER_SIZE;

  mDataBuffer = NULL;
  mUsbDataBuffer = NULL;
  mFlashDataBuffer = NULL;

  DEBUG ((EFI_D_INFO, "Fastboot: Initializing...\n"));

  /* Hook gBS->SetWatchdogTimer FIRST so Phoenix's later 60s re-arm
   * gets clamped to 0. Save the original pointer, swap the slot, then
   * make a normal SetWatchdogTimer(0,...) call which now goes through
   * our clamp — confirms the swap took and disables the timer in one
   * step. */
  if (gBS->SetWatchdogTimer != FastbootWdogTimerClamp) {
    gOriginalSetWatchdogTimer = gBS->SetWatchdogTimer;
    gBS->SetWatchdogTimer     = FastbootWdogTimerClamp;
    DEBUG ((EFI_D_INFO,
            "Fastboot: SetWatchdogTimer hooked (orig=%p clamp=%p)\n",
            gOriginalSetWatchdogTimer, FastbootWdogTimerClamp));
  }

  /* Disable watchdog (now routed through our clamp). */
  Status = gBS->SetWatchdogTimer (0, 0x10000, 0, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Fastboot: Couldn't disable watchdog timer: %r\n",
            Status));
  }

  /* The standard SetWatchdogTimer above only stops the EDK2 / ARM
   * Generic Watchdog. Qualcomm SoCs also run a hardware watchdog
   * (QcomWDogDxe) and on canoe OnePlus Phoenix periodically re-arms
   * it from an OEM thread we have no source for. Triple-disable below
   * (Disable + SetBiteTimeout(0) + periodic 5s pet) is defensive
   * depth: if the first two don't fully take, the periodic pet keeps
   * the bite timer fed indefinitely. */
  {
    EFI_STATUS WdogStatus = gBS->LocateProtocol (&gQcomWdogGuid, NULL,
                                                 (VOID **)&gFastbootWdog);
    if (EFI_ERROR (WdogStatus) || gFastbootWdog == NULL) {
      DEBUG ((EFI_D_WARN,
              "Fastboot: QcomWDog protocol not found: %r\n", WdogStatus));
      gFastbootWdog = NULL;
    } else {
      DEBUG ((EFI_D_INFO,
              "Fastboot: QcomWDog Ver=0x%lx, applying triple-disable\n",
              gFastbootWdog->Version));

      /* (1) Disable: header says it stops watchdog and pet timer. */
      if (gFastbootWdog->Disable != NULL) {
        WdogStatus = gFastbootWdog->Disable ();
        DEBUG ((EFI_D_INFO,
                "Fastboot: QcomWDog->Disable() %r\n", WdogStatus));
      }

      /* (2) SetBiteTimeout(0): header explicitly says 0 cancels the
       * bite timer. Belt-and-suspenders with Disable(), since on canoe
       * Disable() returns Success but the device still resets ~30s
       * later -- suggests the bite countdown isn't actually halted. */
      if (gFastbootWdog->SetBiteTimeout != NULL) {
        WdogStatus = gFastbootWdog->SetBiteTimeout (0);
        DEBUG ((EFI_D_INFO,
                "Fastboot: QcomWDog->SetBiteTimeout(0) %r\n", WdogStatus));
      }

      /* (3) Periodic pet: even if (1) and (2) don't fully take, OnePlus
       * Phoenix re-arms the watchdog from its own thread. Our 5s pet
       * wins as long as it fires faster than the bite timer. Event
       * leaks at fastboot exit but fastboot never EBSes. */
      if (gFastbootWdog->ForceWDogPet != NULL) {
        WdogStatus = gBS->CreateEvent (
                            EVT_TIMER | EVT_NOTIFY_SIGNAL,
                            TPL_CALLBACK,
                            FastbootWdogPetCallback,
                            NULL,
                            &gFastbootWdogPet
                            );
        if (!EFI_ERROR (WdogStatus)) {
          /* 5s in 100ns units. */
          WdogStatus = gBS->SetTimer (gFastbootWdogPet, TimerPeriodic,
                                      50000000ULL);
          DEBUG ((EFI_D_INFO,
                  "Fastboot: QcomWDog 5s pet event armed: %r\n",
                  WdogStatus));
        } else {
          DEBUG ((EFI_D_WARN,
                  "Fastboot: QcomWDog CreateEvent failed: %r\n",
                  WdogStatus));
        }
      }
    }
  }

  /* Create event to pass to FASTBOOT_PROTOCOL.Send, signalling a fatal error */
  Status = gBS->CreateEvent (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, FatalErrorNotify,
                             NULL, &mFatalSendErrorEvent);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Couldn't create Fastboot protocol send event: %r\n",
            Status));
    return Status;
  }

  /* Get the Max/Min download size for low memory */
  GetBufferSize (&MaxBufferSize, &MinBufferSize);

  /* Allocate buffer used to store images passed by the download command */
  GetMaxAllocatableMemory (&MaxDownLoadSize);
  if (!MaxDownLoadSize) {
    DEBUG ((EFI_D_ERROR, "Failed to get free memory for fastboot buffer\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  do {
    // Try allocating 3/4th of free memory available.
    MaxDownLoadSize = EFI_FREE_MEM_DIVISOR (MaxDownLoadSize);
    MaxDownLoadSize = LOCAL_ROUND_TO_PAGE (MaxDownLoadSize, EFI_PAGE_SIZE);
    if (MaxDownLoadSize < MinBufferSize) {
      DEBUG ((EFI_D_ERROR,
        "ERROR: Allocation fail for minimim buffer for fastboot\n"));
      return EFI_OUT_OF_RESOURCES;
    }

    /* If available buffer on target is more than max buffer size,
       we limit this to max buffer buffer size we support */
    if (MaxDownLoadSize > MaxBufferSize) {
      MaxDownLoadSize = MaxBufferSize;
    }

    Status =
        GetFastbootDeviceData ()->UsbDeviceProtocol->AllocateTransferBuffer (
                                      MaxDownLoadSize,
                                      (VOID **)&FastBootBuffer);
  }while (EFI_ERROR (Status));

  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Not enough memory to Allocate Fastboot Buffer\n"));
    return Status;
  }

  /* Clear allocated buffer */
  gBS->SetMem ((VOID *)FastBootBuffer, MaxDownLoadSize , 0x0);
  DEBUG ((EFI_D_VERBOSE,
                  "Fastboot Buffer Size allocated: %ld\n", MaxDownLoadSize));

  MaxDownLoadSize = (CheckRootDeviceType () == NAND) ?
                              MaxDownLoadSize : MaxDownLoadSize / 2;

  FastbootCommandSetup ((VOID *)FastBootBuffer, MaxDownLoadSize);

  InitMultiThreadEnv ();

  return EFI_SUCCESS;
}

/* See header for documentation */
VOID
FastbootRegister (IN CONST CHAR8 *prefix,
                  IN VOID (*handle) (CONST CHAR8 *arg, VOID *data, UINT32 sz))
{
  FASTBOOT_CMD *cmd;

  cmd = AllocateZeroPool (sizeof (*cmd));
  if (cmd) {
    cmd->prefix = prefix;
    cmd->prefix_len = AsciiStrLen (prefix);
    cmd->handle = handle;
    cmd->next = cmdlist;
    cmdlist = cmd;
  } else {
    DEBUG ((EFI_D_VERBOSE,
            "Failed to allocate memory to cmd\n"));
  }
}

STATIC VOID
CmdReboot (IN CONST CHAR8 *arg, IN VOID *data, IN UINT32 sz)
{
  DEBUG ((EFI_D_INFO, "rebooting the device\n"));
  FastbootOkay ("");

  RebootDevice (NORMAL_MODE);

  // Shouldn't get here
  FastbootFail ("Failed to reboot");
}

STATIC VOID
CmdRebootRecovery (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  DEBUG ((EFI_D_INFO, "rebooting the device to recovery\n"));
  FastbootOkay ("");

  RebootDevice (RECOVERY_MODE);

  // Shouldn't get here
  FastbootFail ("Failed to reboot");
}

STATIC VOID
CmdRebootFastboot (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  EFI_STATUS Status = EFI_SUCCESS;
  Status = WriteRecoveryMessage (RECOVERY_BOOT_FASTBOOT);
  if (Status != EFI_SUCCESS) {
    FastbootFail ("Failed to reboot to fastboot mode");
    return;
  }
  DEBUG ((EFI_D_INFO, "rebooting the device to fastbootd\n"));
  FastbootOkay ("");

  RebootDevice (NORMAL_MODE);

  // Shouldn't get here
  FastbootFail ("Failed to reboot");
}

STATIC VOID
CmdOemBcbRecovery (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  EFI_STATUS Status;

  Status = WriteRecoveryMessage (RECOVERY_BOOT_RECOVERY);
  if (EFI_ERROR (Status)) {
    FastbootFail ("Failed to set BCB recovery command");
    return;
  }

  FastbootOkay ("BCB command=boot-recovery");
}

STATIC VOID
CmdOemBcbFastboot (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  EFI_STATUS Status;

  Status = WriteRecoveryMessage (RECOVERY_BOOT_FASTBOOT);
  if (EFI_ERROR (Status)) {
    FastbootFail ("Failed to set BCB fastboot command");
    return;
  }

  FastbootOkay ("BCB command=boot-fastboot");
}

STATIC VOID
CmdOemBcbClear (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  EFI_STATUS             Status;
  EFI_GUID               Ptype = gEfiMiscPartitionGuid;
  MemCardType            CardType;
  struct RecoveryMessage Msg;

  ZeroMem (&Msg, sizeof (Msg));

  CardType = CheckRootDeviceType ();
  if (CardType == NAND) {
    Status = GetNandMiscPartiGuid (&Ptype);
    if (EFI_ERROR (Status)) {
      FastbootFail ("Failed to locate NAND misc partition");
      return;
    }
  }

  Status = WriteToPartition (&Ptype, &Msg, sizeof (Msg));
  if (EFI_ERROR (Status)) {
    FastbootFail ("Failed to clear BCB command");
    return;
  }

  FastbootOkay ("BCB cleared");
}

STATIC VOID
CmdUpdateSnapshot (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  CHAR8 *Command = NULL;
  CONST CHAR8 *Delim = ":";
  EFI_STATUS Status = EFI_SUCCESS;

  Command = AsciiStrStr (Arg, Delim);
  if (Command) {
    Command++;

    if (!AsciiStrnCmp (Command, "merge", AsciiStrLen ("merge"))) {
      if (GetSnapshotMergeStatus () == MERGING) {
        CmdRebootFastboot (Arg, Data, Size);
      }
      FastbootOkay ("");
      return;
    } else if (!AsciiStrnCmp (Command, "cancel", AsciiStrLen ("cancel"))) {
      if (!IsUnlocked ()) {
        FastbootFail ("Snapshot Cancel is not allowed in Lock State");
        return;
      }

      Status = SetSnapshotMergeStatus (CANCELLED);
      if (Status != EFI_SUCCESS) {
        FastbootFail ("Failed to update snapshot state to cancel");
        return;
      }

      //updating fbvar snapshot-merge-state
      AsciiSPrint (SnapshotMergeState,
                    AsciiStrLen (VabSnapshotMergeStatus[NONE_MERGE_STATUS]) + 1,
                    "%a", VabSnapshotMergeStatus[NONE_MERGE_STATUS]);
      FastbootOkay ("");
      return;
    }
  }
  FastbootFail ("Invalid snapshot-update command");
  return;
}

STATIC VOID
CmdContinue (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  EFI_STATUS Status = EFI_SUCCESS;
  CHAR8 Resp[MAX_RSP_SIZE];
  BootInfo Info = {0};

  Info.MultiSlotBoot = PartitionHasMultiSlot ((CONST CHAR16 *)L"boot");
  Status = LoadImageAndAuth (&Info, FALSE, FALSE
  #ifndef USE_DUMMY_BCC
                            , &BccParamsRecvdFromAVB
  #endif
                            );
  if (Status != EFI_SUCCESS) {
    AsciiSPrint (Resp, sizeof (Resp), "Failed to load image from partition: %r",
                 Status);
    FastbootFail (Resp);
    return;
  }

  /* Exit keys' detection firstly */
  ExitMenuKeysDetection ();

  FastbootOkay ("");
  FastbootUsbDeviceStop ();
  Finished = TRUE;
  // call start Linux here
  BootLinux (&Info);
}

STATIC VOID UpdateGetVarVariable (VOID)
{
  BOOLEAN BatterySocOk = FALSE;
  UINT32 BatteryVoltage = 0;

  BatterySocOk = TargetBatterySocOk (&BatteryVoltage);
  AsciiSPrint (StrBatteryVoltage, sizeof (StrBatteryVoltage), "%d",
               BatteryVoltage);
  AsciiSPrint (StrBatterySocOk, sizeof (StrBatterySocOk), "%a",
               BatterySocOk ? "yes" : "no");
  AsciiSPrint (ChargeScreenEnable, sizeof (ChargeScreenEnable), "%d",
               IsChargingScreenEnable ());
  AsciiSPrint (OffModeCharge, sizeof (OffModeCharge), "%d",
               IsChargingScreenEnable ());
}

STATIC VOID WaitForTransferComplete (VOID)
{
  USB_DEVICE_EVENT Msg;
  USB_DEVICE_EVENT_DATA Payload;
  UINTN PayloadSize;

  /* Wait for the transfer to complete */
  while (1) {
    GetFastbootDeviceData ()->UsbDeviceProtocol->HandleEvent (&Msg,
            &PayloadSize, &Payload);
    if (UsbDeviceEventTransferNotification == Msg) {
      if (1 == USB_INDEX_TO_EP (Payload.TransferOutcome.EndpointIndex)) {
        if (USB_ENDPOINT_DIRECTION_IN ==
            USB_INDEX_TO_EPDIR (Payload.TransferOutcome.EndpointIndex))
          break;
      }
    }
  }
}

STATIC VOID CmdGetVarAll (VOID)
{
  FASTBOOT_VAR *Var;
  CHAR8 GetVarAll[MAX_RSP_SIZE];

  for (Var = Varlist; Var; Var = Var->next) {
    AsciiStrnCpyS (GetVarAll, sizeof (GetVarAll), Var->name, MAX_RSP_SIZE);
    AsciiStrnCatS (GetVarAll, sizeof (GetVarAll), ":", AsciiStrLen (":"));
    AsciiStrnCatS (GetVarAll, sizeof (GetVarAll), Var->value, MAX_RSP_SIZE);
    FastbootInfo (GetVarAll);
    /* Wait for the transfer to complete */
    WaitForTransferComplete ();
    ZeroMem (GetVarAll, sizeof (GetVarAll));
  }

  FastbootOkay (GetVarAll);
}

#if defined (GBL_EXPERIMENTAL_FASTBOOT_CMDS)
/* Forward declaration — defined later in the GBL_EXPERIMENTAL block */
STATIC BOOLEAN GblCmdGetVarVbmeta (IN CONST CHAR8 *Arg);
#endif

STATIC VOID
CmdGetVar (CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
  FASTBOOT_VAR *Var;
  Slot CurrentSlot;
  CHAR16 PartNameUniStr[MAX_GPT_NAME_SIZE];
  CHAR8 *Token = AsciiStrStr (Arg, "partition-");
  CHAR8 CurrentSlotAsc[MAX_SLOT_SUFFIX_SZ];

  UpdateGetVarVariable ();

  if (!(AsciiStrCmp ("all", Arg))) {
    CmdGetVarAll ();
    return;
  }

  if (Token) {
    Token = AsciiStrStr (Arg, ":");
    if (Token) {
      Token = Token + AsciiStrLen (":");
      if (AsciiStrLen (Token) >= ARRAY_SIZE (PartNameUniStr)) {
        FastbootFail ("Invalid partition name");
        return;
      }

      AsciiStrToUnicodeStr (Token, PartNameUniStr);

      if (PartitionHasMultiSlot (PartNameUniStr)) {
        CurrentSlot = GetCurrentSlotSuffix ();
        UnicodeStrToAsciiStr (CurrentSlot.Suffix, CurrentSlotAsc);
        AsciiStrnCatS ((CHAR8 *)Arg,
                        MAX_FASTBOOT_COMMAND_SIZE - AsciiStrLen ("getvar:"),
                        CurrentSlotAsc,
                        AsciiStrLen (CurrentSlotAsc));
      }
    }
  }

  for (Var = Varlist; Var; Var = Var->next) {
    if (!AsciiStrCmp (Var->name, Arg)) {
      FastbootOkay (Var->value);
      return;
    }
  }

#if defined (GBL_EXPERIMENTAL_FASTBOOT_CMDS)
  /* Handle dynamic vbmeta:* variables */
  if (AsciiStrnCmp (Arg, "vbmeta:", AsciiStrLen ("vbmeta:")) == 0) {
    if (GblCmdGetVarVbmeta (Arg))
      return;
  }
#endif

  FastbootFail ("GetVar Variable Not found");
}

#ifdef ENABLE_BOOT_CMD
STATIC VOID
CmdBoot (CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
  boot_img_hdr *hdr = Data;
  boot_img_hdr_v3 *HdrV3 = Data;
  EFI_STATUS Status = EFI_SUCCESS;
  UINT32 ImageSizeActual = 0;
  UINT32 SigActual = SIGACTUAL;
  CHAR8 Resp[MAX_RSP_SIZE];
  BOOLEAN MdtpActive = FALSE;
  BootInfo Info = {0};

  if (FixedPcdGetBool (EnableMdtpSupport)) {
    Status = IsMdtpActive (&MdtpActive);

    if (EFI_ERROR (Status)) {
      FastbootFail (
          "Failed to get MDTP activation state, blocking fastboot boot");
      return;
    }

    if (MdtpActive == TRUE) {
      FastbootFail (
          "Fastboot boot command is not available while MDTP is active");
      return;
    }
  }
  if (!IsUnlocked ()) {
    FastbootFail (
          "Fastboot boot command is not available in locked device");
      return;
  }
  if (Size < sizeof (boot_img_hdr)) {
    FastbootFail ("Invalid Boot image Header");
    return;
  }

  if (hdr->header_version <= BOOT_HEADER_VERSION_TWO) {
    hdr->cmdline[BOOT_ARGS_SIZE - 1] = '\0';
  } else {
    HdrV3->cmdline[BOOT_ARGS_SIZE + BOOT_EXTRA_ARGS_SIZE - 1] = '\0';
  }

  SetBootDevImage ();

  Info.Images[0].ImageBuffer = Data;
  /* The actual image size will be updated in LoadImageAndAuth */
  Info.Images[0].ImageSize = Size;
  Info.Images[0].Name = "boot";
  Info.NumLoadedImages = 1;
  Info.MultiSlotBoot = PartitionHasMultiSlot (L"boot");

  if (Info.MultiSlotBoot) {
    Status = ClearUnbootable ();
    if (Status != EFI_SUCCESS) {
      FastbootFail ("CmdBoot: ClearUnbootable failed");
      goto out;
    }
  }

  Status = LoadImageAndAuth (&Info, FALSE, FALSE
  #ifndef USE_DUMMY_BCC
                            , &BccParamsRecvdFromAVB
  #endif
                            );
  if (Status != EFI_SUCCESS) {
    AsciiSPrint (Resp, sizeof (Resp),
                 "Failed to load/authenticate boot image: %r", Status);
    FastbootFail (Resp);
    goto out;
  }

  ImageSizeActual = Info.Images[0].ImageSize;

  if (ImageSizeActual > Size) {
    FastbootFail ("BootImage is Incomplete");
    goto out;
  }

  if (MaxDownLoadSize < (ImageSizeActual - SigActual)) {
    FastbootFail ("BootImage: Size is greater than max download size");
    goto out;
  }

  /* Exit keys' detection firstly */
  ExitMenuKeysDetection ();

  FastbootOkay ("");
  FastbootUsbDeviceStop ();
  ResetBootDevImage ();
  BootLinux (&Info);

out:
  ResetBootDevImage ();
  return;
}
#endif

STATIC VOID
CmdRebootBootloader (CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
  DEBUG ((EFI_D_INFO, "Rebooting the device into bootloader mode\n"));
  FastbootOkay ("");
  RebootDevice (FASTBOOT_MODE);

  // Shouldn't get here
  FastbootFail ("Failed to reboot");
}

#if (defined(ENABLE_DEVICE_CRITICAL_LOCK_UNLOCK_CMDS) ||                       \
     defined(ENABLE_UPDATE_PARTITIONS_CMDS))
STATIC UINT8
is_display_supported ( VOID )
{
  EFI_STATUS Status = EFI_SUCCESS;
  EfiQcomDisplayUtilsProtocol *pDisplayUtilProtocol;
  EFI_GUID DisplayUtilGUID = EFI_DISPLAYUTILS_PROTOCOL_GUID;
  EFI_DISPLAY_UTILS_PANEL_CONFIG_PARAMS PanelConfig;
  UINT32 Index = 0;
  UINT32 ParamSize = sizeof (PanelConfig);
  PanelConfig.uPanelIndex = Index;

  if (EFI_SUCCESS == gBS->LocateProtocol (&DisplayUtilGUID,
                                    NULL,
                                    (VOID **)&pDisplayUtilProtocol)) {
     Status = pDisplayUtilProtocol->DisplayUtilsGetProperty (
                                     EFI_DISPLAY_UTILS_PANEL_CONFIG,
                                    (VOID*)&PanelConfig, &ParamSize);
     if ( Status == EFI_NOT_FOUND ) {
       DEBUG ((EFI_D_VERBOSE, "Display is not supported\n"));
       return 0;
     }
   }
   DEBUG ((EFI_D_VERBOSE, "Display is enabled\n"));
   return 1;
}

STATIC VOID
SetDeviceUnlock (UINT32 Type, BOOLEAN State)
{
  BOOLEAN is_unlocked = FALSE;
  char response[MAX_RSP_SIZE] = {0};
  EFI_STATUS Status;

  if (Type == UNLOCK)
    is_unlocked = IsUnlocked ();
  else if (Type == UNLOCK_CRITICAL)
    is_unlocked = IsUnlockCritical ();
  if (State == is_unlocked) {
    AsciiSPrint (response, MAX_RSP_SIZE, "\tDevice already : %a",
                 (State ? "unlocked!" : "locked!"));
    FastbootFail (response);
    return;
  }

  /* If State is TRUE that means set the unlock to true */
  if (State && !IsAllowUnlock) {
    FastbootFail ("Flashing Unlock is not allowed\n");
    return;
  }

  if (GetAVBVersion () != AVB_LE &&
      is_display_supported () &&
      IsEnableDisplayMenuFlagSupported ()) {
    Status = DisplayUnlockMenu (Type, State);
    if (Status != EFI_SUCCESS) {
      FastbootFail ("Command not support: the display is not enabled");
      return;
    } else {
      FastbootOkay ("");
    }
  } else {
    Status = SetDeviceUnlockValue (Type, State);
    if (Status != EFI_SUCCESS) {
         AsciiSPrint (response, MAX_RSP_SIZE, "\tSet device %a failed: %r",
                  (State ? "unlocked!" : "locked!"), Status);
         FastbootFail (response);
         return;
    }
    FastbootOkay ("");
    if (GetAVBVersion () != AVB_LE &&
       !IsEnableDisplayMenuFlagSupported ()) {
      RebootDevice (RECOVERY_MODE);
    }
  }
}
#endif

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
STATIC VOID
CmdFlashingUnlock (CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
  SetDeviceUnlock (UNLOCK, TRUE);
}

STATIC VOID
CmdFlashingLock (CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
  SetDeviceUnlock (UNLOCK, FALSE);
}
#endif

#ifdef ENABLE_DEVICE_CRITICAL_LOCK_UNLOCK_CMDS
STATIC VOID
CmdFlashingLockCritical (CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
  SetDeviceUnlock (UNLOCK_CRITICAL, FALSE);
}

STATIC VOID
CmdFlashingUnLockCritical (CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
  SetDeviceUnlock (UNLOCK_CRITICAL, TRUE);
}
#endif

STATIC VOID
CmdOemEnableChargerScreen (CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
  EFI_STATUS Status;
  DEBUG ((EFI_D_INFO, "Enabling Charger Screen\n"));

  Status = EnableChargingScreen (TRUE);
  if (Status != EFI_SUCCESS) {
    FastbootFail ("Failed to enable charger screen");
  } else {
    FastbootOkay ("");
  }
}

STATIC VOID
CmdOemDisableChargerScreen (CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
  EFI_STATUS Status;
  DEBUG ((EFI_D_INFO, "Disabling Charger Screen\n"));

  Status = EnableChargingScreen (FALSE);
  if (Status != EFI_SUCCESS) {
    FastbootFail ("Failed to disable charger screen");
  } else {
    FastbootOkay ("");
  }
}

STATIC VOID
CmdOemOffModeCharger (CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
  CHAR8 *Ptr = NULL;
  CONST CHAR8 *Delim = " ";
  EFI_STATUS Status;
  BOOLEAN IsEnable = FALSE;
  CHAR8 Resp[MAX_RSP_SIZE] = "Set off mode charger: ";

  if (Arg) {
    Ptr = AsciiStrStr (Arg, Delim);
    if (Ptr) {
      Ptr++;
      if (!AsciiStrCmp (Ptr, "0"))
        IsEnable = FALSE;
      else if (!AsciiStrCmp (Ptr, "1"))
        IsEnable = TRUE;
      else {
        FastbootFail ("Invalid input entered");
        return;
      }
    } else {
      FastbootFail ("Enter fastboot oem off-mode-charge 0/1");
      return;
    }
  } else {
    FastbootFail ("Enter fastboot oem off-mode-charge 0/1");
    return;
  }

  AsciiStrnCatS (Resp, sizeof (Resp), Arg, AsciiStrLen (Arg));
  /* update charger_screen_enabled value for getvar command */
  Status = EnableChargingScreen (IsEnable);
  if (Status != EFI_SUCCESS) {
    AsciiStrnCatS (Resp, sizeof (Resp), ": failed", AsciiStrLen (": failed"));
    FastbootFail (Resp);
  } else {
    AsciiStrnCatS (Resp, sizeof (Resp), ": done", AsciiStrLen (": done"));
    FastbootOkay (Resp);
  }
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

STATIC VOID
CmdOemSetHwFenceValue (CONST CHAR8 *arg, VOID *data, UINT32 Size)
{
  EFI_STATUS Status;
  CHAR8 Resp[MAX_RSP_SIZE] = "Set HW fence value: ";
  CHAR8 HwFenceValue[MAX_DISPLAY_PANEL_OVERRIDE] = " msm_hw_fence.enable=";
  INTN Pos = 0;

  for (Pos = 0; Pos < AsciiStrLen (arg); Pos++) {
    if (arg[Pos] == ' ') {
      arg++;
      Pos--;
    } else {
      break;
    }
  }

  if ((AsciiStrLen(arg) != 1) || (arg[0] != '0' && arg[0] != '1')) {
    AsciiStrnCatS (Resp, sizeof (Resp), "invalid input (must be 0 or 1)",
                  AsciiStrLen ("invalid input (must be 0 or 1)"));
    FastbootFail (Resp);
    return;
  }

  AsciiStrnCatS (HwFenceValue,
                 MAX_DISPLAY_PANEL_OVERRIDE,
                 arg,
                 AsciiStrLen (arg));

  Status = gRT->SetVariable ((CHAR16 *)L"HwFenceConfiguration",
                               &gQcomTokenSpaceGuid,
                               EFI_VARIABLE_RUNTIME_ACCESS |
                               EFI_VARIABLE_BOOTSERVICE_ACCESS |
                               EFI_VARIABLE_NON_VOLATILE,
                               AsciiStrLen (HwFenceValue),
                               (VOID *)HwFenceValue);

  if (EFI_ERROR (Status)) {
    AsciiStrnCatS (Resp, sizeof (Resp), ": failed!", AsciiStrLen (": failed!"));
    FastbootFail (Resp);
  } else {
    AsciiStrnCatS (Resp, sizeof (Resp), ": done", AsciiStrLen (": done"));
    FastbootOkay (Resp);
  }
}

STATIC VOID
CmdOemSetGpuPreemptionValue (CONST CHAR8 *arg, VOID *data, UINT32 Size)
{
  EFI_STATUS Status;
  CHAR8 Resp[MAX_RSP_SIZE] = "Set GPU HW Preemption: ";
  CHAR8 GpuPreemptionValue[MAX_GPU_CONFIG_OVERRIDE] =
          " msm_kgsl.preempt_enable=";
  INTN Pos = 0;

  for (Pos = 0; Pos < AsciiStrLen (arg); Pos++) {
    if (arg[Pos] == ' ') {
      arg++;
      Pos--;
    } else {
      break;
    }
  }

  if ((AsciiStrLen(arg) != 1) || (arg[0] != '0' && arg[0] != '1')) {
	  FastbootFail("Set GPU HW Preemption: Invalid Argument, Value must be 1 or 0");
	  return;
  }

  AsciiStrnCatS (GpuPreemptionValue,
                 MAX_GPU_CONFIG_OVERRIDE,
                 arg,
                 AsciiStrLen (arg));

  Status = gRT->SetVariable ((CHAR16 *)L"GpuConfiguration",
                               &gQcomTokenSpaceGuid,
                               EFI_VARIABLE_RUNTIME_ACCESS |
                               EFI_VARIABLE_BOOTSERVICE_ACCESS |
                               EFI_VARIABLE_NON_VOLATILE,
                               AsciiStrLen (GpuPreemptionValue),
                               (VOID *)GpuPreemptionValue);

  if (EFI_ERROR (Status)) {
    AsciiStrnCatS (Resp, sizeof (Resp), ": failed!", AsciiStrLen (": failed!"));
    FastbootFail (Resp);
  } else {
    AsciiStrnCatS (Resp, sizeof (Resp), ": done", AsciiStrLen (": done"));
    FastbootOkay (Resp);
  }
}

STATIC VOID
CmdOemAudioFrameWork (CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
  EFI_STATUS Status;

  if (Arg == NULL || Arg[0] == '\0') {
    FastbootFail ("Invalid audio-framework command");
    return;
  }

  if (Arg[0] == ' ') {
    Arg++;
  }

  if (Arg[0] == '\0') {
    FastbootFail ("Invalid audio-framework command");
    return;
  }

  if (!IsAllowedAudioFramework (Arg)) {
    FastbootFail ("invalid Audio framework");
     return;
  }

  Status = StoreAudioFrameWork (Arg, (UINT32)AsciiStrLen (Arg));
  if (Status != EFI_SUCCESS) {
    FastbootFail ("Failed to store Audio framework");
  } else {
    FastbootOkay ("");
  }
}

/*
 * Input validation helpers for display panel override fastboot OEM command.
 * These are intentionally permissive enough to support real panel naming
 * conventions (e.g., prim:<panel>, :sec:<panel>, :sec0:<panel>) while blocking
 * whitespace/control characters and common command-injection primitives.
*/
#define MAX_FASTBOOT_OEM_ARG_SCAN  (MAX_DISPLAY_PANEL_OVERRIDE)

STATIC UINTN
AsciiStrnLenSafe (IN CONST CHAR8 *Str,
  IN UINTN MaxLen)
{
  UINTN i;

  if (Str == NULL) {
    return 0;
  }

  for (i = 0; i < MaxLen; i++) {
    if (Str[i] == '\0') {
      return i;
    }
  }

  return MaxLen;
}

STATIC BOOLEAN
IsValidPanelChar (IN CHAR8 c)
{
  if ((c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9')) {
    return TRUE;
  }

  // Allow separators used in existing panel strings.
  switch (c) {
    case '_':
    case '-':
    case '.':
    case ':':
    case '/':
    case ',':
    case '@':
    case '#':
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC BOOLEAN
IsValidPanelString (IN CONST CHAR8 *Str,
  IN UINTN Len)
{
  UINTN i;

  if (Str == NULL || Len == 0) {
    return FALSE;
  }

  for (i = 0; i < Len; i++) {
    // Reject whitespace and control characters outright.
    if ((Str[i] <= 0x20) || (Str[i] == 0x7F)) {
      return FALSE;
    }

    if (!IsValidPanelChar (Str[i])) {
      return FALSE;
    }
  }

  return TRUE;
}

STATIC VOID
CmdOemSelectDisplayPanel (CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
  EFI_STATUS Status;
  CHAR8 resp[MAX_RSP_SIZE] = "Selecting Panel: ";
  CHAR8 DisplayPanelStr[MAX_DISPLAY_PANEL_OVERRIDE] = "";
  CHAR8 DisplayPanelStrExist[MAX_DISPLAY_PANEL_OVERRIDE] = "";
  CONST CHAR8 *ArgTrim = NULL;
  UINTN ArgTrimLen = 0;
  UINTN CurStrLen = 0;
  UINTN ExistingLen = 0;
  UINTN TotalStrLen = 0;
  BOOLEAN Append = FALSE;

  // Basic input validation
  if (arg == NULL) {
    AsciiStrnCatS (resp, sizeof (resp), ": invalid args", AsciiStrLen (": invalid args"));
    FastbootFail (resp);
    return;
  }

  // Trim leading spaces only (preserve a leading ':' used for append semantics)
  ArgTrim = arg;
  while (*ArgTrim == ' ') {
    ArgTrim++;
  }

  // Enforce bounded length and NUL termination within scan limit
  ArgTrimLen = AsciiStrnLenSafe (ArgTrim, MAX_FASTBOOT_OEM_ARG_SCAN);
  if (ArgTrimLen == 0 || ArgTrimLen >= MAX_FASTBOOT_OEM_ARG_SCAN) {
    AsciiStrnCatS (resp, sizeof (resp), ": invalid/too long", AsciiStrLen (": invalid/too long"));
    FastbootFail (resp);
    return;
  }

  // Validate content: allow-list characters, reject whitespace/control chars
  if (!IsValidPanelString (ArgTrim, ArgTrimLen)) {
    AsciiStrnCatS (resp, sizeof (resp), ": invalid panel", AsciiStrLen (": invalid panel"));
    FastbootFail (resp);
    return;
  }

  // Append mode if first non-space character is ':'
  if (ArgTrim[0] == ':') {
    Append = TRUE;
  }

  if (Append) {
    CurStrLen = sizeof (DisplayPanelStrExist);
    Status = DisplayGetVariable ((CHAR16 *)L"DisplayPanelOverride",
                                 (VOID *)DisplayPanelStrExist,
                                 &CurStrLen);

    // Defensive NUL termination before bounded strlen
    DisplayPanelStrExist[sizeof (DisplayPanelStrExist) - 1] = '\0';
    ExistingLen = AsciiStrnLenSafe (DisplayPanelStrExist, sizeof (DisplayPanelStrExist));

    // If variable read failed, we still allow setting only the provided value.
    // But if the existing value is non-terminated/too long, fail rather than risk overflow.
    if (!EFI_ERROR (Status) && ExistingLen > 0) {
      if (ExistingLen >= MAX_DISPLAY_PANEL_OVERRIDE) {
        AsciiStrnCatS (resp, sizeof (resp), ": existing too long", AsciiStrLen (": existing too long"));
        FastbootFail (resp);
        return;
      }

      TotalStrLen = ExistingLen + ArgTrimLen;
      if (TotalStrLen >= MAX_DISPLAY_PANEL_OVERRIDE) {
        AsciiStrnCatS (resp, sizeof (resp), ": too long", AsciiStrLen (": too long"));
        FastbootFail (resp);
        return;
      }

      Status = AsciiStrnCatS (DisplayPanelStr,
                             MAX_DISPLAY_PANEL_OVERRIDE,
                             DisplayPanelStrExist,
                             ExistingLen);
      if (EFI_ERROR (Status)) {
        AsciiStrnCatS (resp, sizeof (resp), ": failed", AsciiStrLen (": failed"));
        FastbootFail (resp);
        return;
      }

      DEBUG ((EFI_D_INFO, "existing panel name (%a)", DisplayPanelStr));
    }
  }

  Status = AsciiStrnCatS (DisplayPanelStr,
                         MAX_DISPLAY_PANEL_OVERRIDE,
                         ArgTrim,
                         ArgTrimLen);
  if (EFI_ERROR (Status)) {
    AsciiStrnCatS (resp, sizeof (resp), ": failed", AsciiStrLen (": failed"));
    FastbootFail (resp);
    return;
  }

  /* Update the environment variable with the selected panel */
  Status = DisplaySetVariable ((CHAR16 *)L"DisplayPanelOverride",
                               (VOID *)DisplayPanelStr,
                               AsciiStrLen (DisplayPanelStr));

  if (EFI_ERROR (Status)) {
    AsciiStrnCatS (resp, sizeof (resp), ": failed", AsciiStrLen (": failed"));
    FastbootFail (resp);
  } else {
    AsciiStrnCatS (resp, sizeof (resp), ": done", AsciiStrLen (": done"));
    FastbootOkay (resp);
  }
}

#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
STATIC VOID
CmdFlashingGetUnlockAbility (CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
  CHAR8 UnlockAbilityInfo[MAX_RSP_SIZE];

  AsciiSPrint (UnlockAbilityInfo, sizeof (UnlockAbilityInfo),
               "get_unlock_ability: %d", IsAllowUnlock);
  FastbootInfo (UnlockAbilityInfo);
  WaitForTransferComplete ();
  FastbootOkay ("");
}
#endif

#if HIBERNATION_SUPPORT_NO_AES
STATIC VOID
CmdGoldenSnapshot (CONST CHAR8 *Arg, VOID *Data, UINT32 Size)
{
  EFI_STATUS Status;
  CHAR8 *Ptr = NULL;
  CONST CHAR8 *Delim = " ";

  if (Arg) {
    /* Expect a string "enable" or "disable" */
    if (((AsciiStrLen (Arg)) < 7)
        ||
        ((AsciiStrLen (Arg)) > 8) ) {
      FastbootFail ("Invalid input entered");
      return;
    }
    Ptr = AsciiStrStr (Arg, Delim);
    Ptr++;
  } else {
    FastbootFail ("Invalid input entered");
    return;
  }

  if (!AsciiStrCmp (Ptr, "enable")) {
    /* Set a magic value 200 to denote if it is golden image */
    Status = SetSnapshotGolden (200);
  }
  else if (!AsciiStrCmp (Ptr, "disable")) {
    Status = SetSnapshotGolden (0);
  }
  else {
    FastbootFail ("Invalid input entered");
    return;
  }

  if (Status != EFI_SUCCESS) {
    FastbootFail ("Failed to update golden-snapshot flag");
  }
  else {
    FastbootOkay ("Golden-snapshot flag updated");
  }
   return;
}
#endif

STATIC VOID
CmdOemDevinfo (CONST CHAR8 *arg, VOID *data, UINT32 sz)
{
  CHAR8 DeviceInfo[MAX_RSP_SIZE];

  AsciiSPrint (DeviceInfo, sizeof (DeviceInfo), "Verity mode: %a",
               IsEnforcing () ? "true" : "false");
  FastbootInfo (DeviceInfo);
  WaitForTransferComplete ();
  AsciiSPrint (DeviceInfo, sizeof (DeviceInfo), "Device unlocked: %a",
               IsUnlocked () ? "true" : "false");
  FastbootInfo (DeviceInfo);
  WaitForTransferComplete ();
  AsciiSPrint (DeviceInfo, sizeof (DeviceInfo), "Device critical unlocked: %a",
               IsUnlockCritical () ? "true" : "false");
  FastbootInfo (DeviceInfo);
  WaitForTransferComplete ();
  AsciiSPrint (DeviceInfo, sizeof (DeviceInfo), "Charger screen enabled: %a",
               IsChargingScreenEnable () ? "true" : "false");
  FastbootInfo (DeviceInfo);
  WaitForTransferComplete ();

  if (IsHibernationEnabled ()) {
    AsciiSPrint (DeviceInfo, sizeof (DeviceInfo), "Erase swap on restore: %a",
                 IsSnapshotGolden () ? "true" : "false");
    FastbootInfo (DeviceInfo);
    WaitForTransferComplete ();
  }

  FastbootOkay ("");
}

STATIC EFI_STATUS
AcceptCmdTimerInit (IN UINT64 Size, IN CHAR8 *Data)
{
  EFI_STATUS Status = EFI_SUCCESS;
  CmdInfo *AcceptCmdInfo = NULL;
  EFI_EVENT CmdEvent = NULL;

  AcceptCmdInfo = AllocateZeroPool (sizeof (CmdInfo));
  if (!AcceptCmdInfo)
    return EFI_OUT_OF_RESOURCES;

  AcceptCmdInfo->Size = Size;
  AcceptCmdInfo->Data = Data;

  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                             AcceptCmdHandler, AcceptCmdInfo, &CmdEvent);

  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (CmdEvent, TimerRelative, 100000);
  }

  if (EFI_ERROR (Status)) {
    FreePool (AcceptCmdInfo);
    AcceptCmdInfo = NULL;
  }

  return Status;
}

STATIC VOID
AcceptCmdHandler (IN EFI_EVENT Event, IN VOID *Context)
{
  CmdInfo *AcceptCmdInfo = Context;

  if (Event) {
    gBS->SetTimer (Event, TimerCancel, 0);
    gBS->CloseEvent (Event);
  }

  AcceptCmd (AcceptCmdInfo->Size, AcceptCmdInfo->Data);
  FreePool (AcceptCmdInfo);
  AcceptCmdInfo = NULL;
}

STATIC VOID
AcceptCmd (IN UINT64 Size, IN CHAR8 *Data)
{
  EFI_STATUS Status = EFI_SUCCESS;
  FASTBOOT_CMD *cmd;
  UINT32 BatteryVoltage = 0;
  STATIC BOOLEAN IsFirstEraseFlash;
  CHAR8 FlashResultStr[MAX_RSP_SIZE] = "\0";

  if (!Data) {
    FastbootFail ("Invalid input command");
    return;
  }
  if (Size > MAX_FASTBOOT_COMMAND_SIZE)
    Size = MAX_FASTBOOT_COMMAND_SIZE;
  Data[Size] = '\0';

  DEBUG ((EFI_D_INFO, "Handling Cmd: %a\n", Data));

  if (!IsDisableParallelDownloadFlash ()) {
    /* Wait for flash finished before next command */
    if (AsciiStrnCmp (Data, "download", AsciiStrLen ("download"))) {
      StopUsbTimer ();
      if (!IsFlashComplete &&
          !IsUseMThreadParallel ()) {
        Status = AcceptCmdTimerInit (Size, Data);
        if (Status == EFI_SUCCESS) {
          return;
        }
      }
    }

    /* Check last flash result */
    if (FlashResult != EFI_SUCCESS) {
      AsciiSPrint (FlashResultStr, MAX_RSP_SIZE, "%a : %r",
                 "Error: Last flash failed", FlashResult);

      DEBUG ((EFI_D_ERROR, "%a\n", FlashResultStr));
      if (!AsciiStrnCmp (Data, "flash", AsciiStrLen ("flash")) ||
          !AsciiStrnCmp (Data, "download", AsciiStrLen ("download"))) {
        FastbootFail (FlashResultStr);
        FlashResult = EFI_SUCCESS;
        return;
      }
    }
  }

  if (FixedPcdGetBool (EnableBatteryVoltageCheck)) {
    /* Check battery voltage before erase or flash image
     * It gets partition type once when to flash or erase image,
     * for sparse image, it calls flash command more than once, it's
     * no need to check the battery voltage at every time, it's risky
     * to stop the update when the image is half-flashed.
     */
    if (IsFirstEraseFlash) {
      if (!AsciiStrnCmp (Data, "erase", AsciiStrLen ("erase")) ||
          !AsciiStrnCmp (Data, "flash", AsciiStrLen ("flash"))) {
        if (!TargetBatterySocOk (&BatteryVoltage)) {
          DEBUG ((EFI_D_VERBOSE, "fastboot: battery voltage: %d\n",
                  BatteryVoltage));
          FastbootFail ("Warning: battery's capacity is very low\n");
          return;
        }
        IsFirstEraseFlash = FALSE;
      }
    } else if (!AsciiStrnCmp (Data, "getvar:partition-type",
                              AsciiStrLen ("getvar:partition-type"))) {
      IsFirstEraseFlash = TRUE;
    }
  }

  for (cmd = cmdlist; cmd; cmd = cmd->next) {
    if (AsciiStrnCmp (Data, cmd->prefix, cmd->prefix_len))
      continue;

    cmd->handle ((CONST CHAR8 *)Data + cmd->prefix_len, (VOID *)mUsbDataBuffer,
                 (UINT32)mBytesReceivedSoFar);
    return;
  }
  DEBUG ((EFI_D_ERROR, "\nFastboot Send Fail\n"));
  FastbootFail ("unknown command");
}

STATIC VOID
CheckPartitionFsSignature (IN CHAR16 *PartName,
                           OUT FS_SIGNATURE *FsSignature)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_HANDLE *Handle = NULL;
  EFI_STATUS Status = EFI_SUCCESS;
  UINT32 BlkSz = 0;
  CHAR8 *FsSuperBlk = NULL;
  CHAR8 *FsSuperBlkBuffer = NULL;
  UINT32 SuperBlkLba = 0;

  *FsSignature = UNKNOWN_FS_SIGNATURE;

  Status = PartitionGetInfo (PartName, &BlockIo, &Handle);
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Failed to Info for %s partition\n", PartName));
    return;
  }
  if (!BlockIo) {
    DEBUG ((EFI_D_ERROR, "BlockIo for %s is corrupted\n", PartName));
    return;
  }

  BlkSz = BlockIo->Media->BlockSize;
  FsSuperBlkBuffer = AllocateZeroPool (BlkSz);
  if (!FsSuperBlkBuffer) {
    DEBUG ((EFI_D_ERROR, "Failed to allocate buffer for superblock %s\n",
                            PartName));
    return;
  }
  FsSuperBlk = FsSuperBlkBuffer;
  SuperBlkLba = (FS_SUPERBLOCK_OFFSET / BlkSz);

  BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId,
                           SuperBlkLba,
                           BlkSz, FsSuperBlkBuffer);

  /* If superblklba is 0, it means super block is part of first block read */
  if (SuperBlkLba == 0) {
    FsSuperBlk += FS_SUPERBLOCK_OFFSET;
  }

  if (*((UINT16 *)&FsSuperBlk[EXT_MAGIC_OFFSET_SB]) == (UINT16)EXT_FS_MAGIC) {
    DEBUG ((EFI_D_VERBOSE, "%s Found EXT FS type\n", PartName));
    *FsSignature = EXT_FS_SIGNATURE;
  } else if (*((UINT32 *)&FsSuperBlk[F2FS_MAGIC_OFFSET_SB]) ==
              (UINT32)F2FS_FS_MAGIC) {
      DEBUG ((EFI_D_VERBOSE, "%s Found F2FS FS type\n", PartName));
      *FsSignature = F2FS_FS_SIGNATURE;
    } else {
        DEBUG ((EFI_D_VERBOSE, "%s No Known FS type Found\n", PartName));
  }

  if (FsSuperBlkBuffer) {
     FreePool (FsSuperBlkBuffer);
  }
  return;
}

STATIC EFI_STATUS
GetPartitionType (IN CHAR16 *PartName, OUT CHAR8 * PartType)
{
  UINT32 LoopCounter;
  CHAR8 AsciiPartName[MAX_GET_VAR_NAME_SIZE];
  FS_SIGNATURE FsSignature;

  if (PartName == NULL ||
      PartType == NULL) {
    DEBUG ((EFI_D_ERROR, "Invalid parameters to GetPartitionType\n"));
    return EFI_INVALID_PARAMETER;
  }

  /* By default copy raw to response */
  AsciiStrnCpyS (PartType, MAX_GET_VAR_NAME_SIZE,
                  RAW_FS_STR, AsciiStrLen (RAW_FS_STR));
  UnicodeStrToAsciiStr (PartName, AsciiPartName);

  /* Mark partition type for hard-coded partitions only */
  for (LoopCounter = 0; LoopCounter < ARRAY_SIZE (part_info); LoopCounter++) {
    /* Check if its a hardcoded partition type */
    if (!AsciiStrnCmp ((CONST CHAR8 *) AsciiPartName,
                          part_info[LoopCounter].part_name,
                          AsciiStrLen (part_info[LoopCounter].part_name))) {
      /* Check filesystem type present on partition */
      CheckPartitionFsSignature (PartName, &FsSignature);
      switch (FsSignature) {
        case EXT_FS_SIGNATURE:
          AsciiStrnCpyS (PartType, MAX_GET_VAR_NAME_SIZE, EXT_FS_STR,
                          AsciiStrLen (EXT_FS_STR));
          break;
        case F2FS_FS_SIGNATURE:
          AsciiStrnCpyS (PartType, MAX_GET_VAR_NAME_SIZE, F2FS_FS_STR,
                          AsciiStrLen (F2FS_FS_STR));
          break;
        case UNKNOWN_FS_SIGNATURE:
          /* Copy default hardcoded type in case unknown partition type */
          AsciiStrnCpyS (PartType, MAX_GET_VAR_NAME_SIZE,
                          part_info[LoopCounter].type_response,
                          AsciiStrLen (part_info[LoopCounter].type_response));
      }
    }
  }
  return EFI_SUCCESS;
}

STATIC EFI_STATUS
GetPartitionSizeViaName (IN CHAR16 *PartName, OUT CHAR8 * PartSize)
{
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_HANDLE *Handle = NULL;
  EFI_STATUS Status = EFI_INVALID_PARAMETER;
  UINT64 PartitionSize;

  Status = PartitionGetInfo (PartName, &BlockIo, &Handle);
  if (Status != EFI_SUCCESS) {
    return Status;
  }

  if (!BlockIo) {
    DEBUG ((EFI_D_ERROR, "BlockIo for %s is corrupted\n", PartName));
    return EFI_VOLUME_CORRUPTED;
  }

  PartitionSize = GetPartitionSize (BlockIo);
  if (!PartitionSize) {
    return EFI_BAD_BUFFER_SIZE;
  }

  AsciiSPrint (PartSize, MAX_RSP_SIZE, " 0x%llx", PartitionSize);
  return EFI_SUCCESS;

}

STATIC EFI_STATUS
PublishGetVarPartitionInfo (
                            IN struct GetVarPartitionInfo *PublishedPartInfo,
                            IN UINT32 NumParts)
{
  UINT32 PtnLoopCount;
  EFI_STATUS Status = EFI_INVALID_PARAMETER;
  EFI_STATUS RetStatus = EFI_SUCCESS;
  CHAR16 *PartitionNameUniCode = NULL;
  BOOLEAN PublishType;
  BOOLEAN PublishSize;

  /* Clear Published Partition Buffer */
  gBS->SetMem (PublishedPartInfo,
          sizeof (struct GetVarPartitionInfo) * MAX_NUM_PARTITIONS, 0);

  /* Loop will go through each partition entry
     and publish info for all partitions.*/
  for (PtnLoopCount = 0; PtnLoopCount < NumParts; PtnLoopCount++) {
    PublishType = FALSE;
    PublishSize = FALSE;
    PartitionNameUniCode = PtnEntries[PtnLoopCount].PartEntry.PartitionName;
    /* Skip Null/last partition */
    if (PartitionNameUniCode[0] == '\0') {
      continue;
    }
    UnicodeStrToAsciiStr (PtnEntries[PtnLoopCount].PartEntry.PartitionName,
                          (CHAR8 *)PublishedPartInfo[PtnLoopCount].part_name);

    /* Fill partition size variable and response string */
    AsciiStrnCpyS (PublishedPartInfo[PtnLoopCount].getvar_size_str,
                      MAX_GET_VAR_NAME_SIZE, "partition-size:",
                      AsciiStrLen ("partition-size:"));
    Status = AsciiStrnCatS (PublishedPartInfo[PtnLoopCount].getvar_size_str,
                            MAX_GET_VAR_NAME_SIZE,
                            PublishedPartInfo[PtnLoopCount].part_name,
                            AsciiStrLen (
                              PublishedPartInfo[PtnLoopCount].part_name));
    if (!EFI_ERROR (Status)) {
      Status = GetPartitionSizeViaName (
                            PartitionNameUniCode,
                            PublishedPartInfo[PtnLoopCount].size_response);
      if (Status == EFI_SUCCESS) {
        PublishSize = TRUE;
      }
    }

    /* Fill partition type variable and response string */
    AsciiStrnCpyS (PublishedPartInfo[PtnLoopCount].getvar_type_str,
                    MAX_GET_VAR_NAME_SIZE, "partition-type:",
                    AsciiStrLen ("partition-type:"));
    Status = AsciiStrnCatS (PublishedPartInfo[PtnLoopCount].getvar_type_str,
                              MAX_GET_VAR_NAME_SIZE,
                              PublishedPartInfo[PtnLoopCount].part_name,
                              AsciiStrLen (
                                PublishedPartInfo[PtnLoopCount].part_name));
    if (!EFI_ERROR (Status)) {
      Status = GetPartitionType (
                            PartitionNameUniCode,
                            PublishedPartInfo[PtnLoopCount].type_response);
      if (Status == EFI_SUCCESS) {
        PublishType = TRUE;
      }
    }

    if (PublishSize) {
      FastbootPublishVar (PublishedPartInfo[PtnLoopCount].getvar_size_str,
                              PublishedPartInfo[PtnLoopCount].size_response);
    } else {
        DEBUG ((EFI_D_ERROR, "Error Publishing size info for %s partition\n",
                                                        PartitionNameUniCode));
        RetStatus = EFI_INVALID_PARAMETER;
    }

    if (PublishType) {
      FastbootPublishVar (PublishedPartInfo[PtnLoopCount].getvar_type_str,
                              PublishedPartInfo[PtnLoopCount].type_response);
    } else {
        DEBUG ((EFI_D_ERROR, "Error Publishing type info for %s partition\n",
                                                        PartitionNameUniCode));
        RetStatus = EFI_INVALID_PARAMETER;
    }
  }
  return RetStatus;
}

STATIC EFI_STATUS
ReadAllowUnlockValue (UINT32 *IsAllowUnlock)
{
  EFI_STATUS Status;
  EFI_BLOCK_IO_PROTOCOL *BlockIo = NULL;
  EFI_HANDLE *Handle = NULL;
  UINT8 *Buffer;

  Status = PartitionGetInfo ((CHAR16 *)L"frp", &BlockIo, &Handle);
  if (Status != EFI_SUCCESS)
    return Status;

  if (!BlockIo)
    return EFI_NOT_FOUND;

  Buffer = AllocateZeroPool (BlockIo->Media->BlockSize);
  if (!Buffer) {
    DEBUG ((EFI_D_ERROR, "Failed to allocate memory for unlock value \n"));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId,
                                BlockIo->Media->LastBlock,
                                BlockIo->Media->BlockSize, Buffer);
  if (Status != EFI_SUCCESS)
    goto Exit;

  /* IsAllowUnlock value stored at the LSB of last byte*/
  *IsAllowUnlock = Buffer[BlockIo->Media->BlockSize - 1] & 0x01;

Exit:
  FreePool (Buffer);
  return Status;
}

/* `oem boot-efi`: LoadImage + StartImage on the contents of the staging
 * buffer. Android `fastboot stage` is just `download:` on the wire — the
 * payload lands in mUsbDataBuffer (set by CmdDownload). The pointer swap
 * + size copy that move it to mFlashDataBuffer/mFlashNumDataBytes happens
 * in ExchangeFlashAndUsbDataBuf(), which CmdFlash calls at its top. We
 * mirror that contract here.

 * Naming convention follows the rest of the OEM verbs in this cmd_list
 * (`oem off-mode-charge`, `oem set-hw-fence-value`, `oem device-info`):
 * kebab-case, not snake_case.
 *
 * Caveat (documented discipline, not enforced in code): a `flash:` or
 * subsequent `download:` between `stage` and `oem boot-efi` will overwrite
 * the buffer. Don't interleave.
 *
 * Nested boot-efi: when the staged image itself calls FastbootInitialize
 * a fresh transfer buffer is allocated for that nested fastboot session
 * (see FastbootCmdsInit:2620), so loading an EFI from inside a loaded
 * EFI is naturally supported.
 *
 * StartImage does not return on a successful boot — control transfers to
 * the staged image. If we get back here it's because the image returned
 * (rare for boot loaders) or LoadImage/StartImage failed.
 */
STATIC VOID
CmdOemBootEfi (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  EFI_STATUS Status;
  EFI_HANDLE ImageHandle = NULL;
  CHAR8 Resp[MAX_RSP_SIZE];

  /* Swap mUsbDataBuffer↔mFlashDataBuffer and copy mNumDataBytes
   * → mFlashNumDataBytes. Without this, mFlashNumDataBytes is still
   * the initial buffer-capacity (~768 MB on canoe) and LoadImage gets
   * a wildly wrong size. */
  ExchangeFlashAndUsbDataBuf ();

  if (mFlashDataBuffer == NULL || mFlashNumDataBytes == 0) {
    FastbootFail ("no staged image — run `fastboot stage <file>` first");
    return;
  }

  AsciiSPrint (Resp, sizeof (Resp),
               "loading staged %llu bytes", mFlashNumDataBytes);
  FastbootInfo (Resp);

  Status = gBS->LoadImage (FALSE, gImageHandle, NULL,
                           mFlashDataBuffer, mFlashNumDataBytes,
                           &ImageHandle);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Resp, sizeof (Resp), "LoadImage failed: %r", Status);
    FastbootFail (Resp);
    return;
  }

  /* Reply OKAY before handing off so the host's fastboot terminates
   * cleanly.  StartImage does not return on a successful boot — control
   * transfers to the staged image permanently.  If it does return (image
   * exited back to UEFI), the rc is logged to ConOut for after-the-fact
   * triage; the host has already moved on and must not receive a second
   * OKAY/FAIL reply. */
  FastbootOkay ("started");
  WaitForTransferComplete ();

  Status = gBS->StartImage (ImageHandle, NULL, NULL);

  /* Only reachable if the staged image returned to its caller rather than
   * chainloading another OS.  Log the rc — do NOT call FastbootOkay/Fail
   * again; the host already saw OKAY above. */
  {
    CHAR8 LogLine[160];
    AsciiSPrint (LogLine, sizeof (LogLine),
                 "oem boot-efi: StartImage returned %r (image exited back to UEFI)\n",
                 Status);
    Print (L"%a", LogLine);
  }
}

#if defined (GBL_EXPERIMENTAL_FASTBOOT_CMDS)

/* `oem escape`: leave AUTO_DEBUG FastbootLib and continue into the patched
 * stock ABL path. This is intentionally BCB-free: host can request a recovery
 * boot using `fastboot reboot recovery`, wait for AUTO_DEBUG's default
 * FastbootLib to reappear, then send `fastboot oem escape`. CmdRebootRecovery
 * uses a recovery reset reason instead of BCB, so the reason is preserved for
 * stock ABL while GBL itself avoids persistent BCB state. */
STATIC VOID
CmdOemEscape (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  EFI_STATUS Status;
  CHAR8 Resp[MAX_RSP_SIZE];

  FastbootInfo ("escaping to patched ABL");
  Status = BootFlowChainLoad ();

  AsciiSPrint (Resp, sizeof (Resp),
               "escape returned %r (falling back to fastboot)", Status);
  FastbootFail (Resp);
}

#define GBL_RPMB_UNLOCK_CONFIRM " CONFIRM_WIPE"

/* Dangerous debug-only direct DevInfo state toggle. This intentionally bypasses
 * the normal fastboot display/FRP confirmation path so we can run locked-state
 * boot experiments and then recover from our own FastbootLib. It still uses the
 * platform SetDeviceUnlockValue() primitive, so it also calls ResetDeviceState()
 * and writes the recovery wipe-data command into misc. Never expose outside
 * debug/testbench builds. */
STATIC VOID
CmdOemRpmbSetUnlockState (
  IN CONST CHAR8 *Arg,
  IN BOOLEAN State
  )
{
  EFI_STATUS Status;
  CHAR8 Resp[MAX_RSP_SIZE];

  if (AsciiStrCmp (Arg, GBL_RPMB_UNLOCK_CONFIRM) != 0) {
    FastbootInfo ("DANGEROUS: ABL NOT PATCHED FOR THIS - ONLY USE IN UNLOCK");
    FastbootInfo ("SetDeviceUnlockValue requests data wipe via misc");
    AsciiSPrint (Resp, sizeof (Resp), "rerun with:%a",
                 GBL_RPMB_UNLOCK_CONFIRM);
    FastbootFail (Resp);
    return;
  }

  FastbootInfo ("DANGEROUS: direct RPMB DevInfo unlock-state write");
  FastbootInfo ("ABL NOT PATCHED FOR THIS - ONLY USE IN UNLOCK");
  FastbootInfo ("SetDeviceUnlockValue also requests data wipe via misc");

  Status = SetDeviceUnlockValue (UNLOCK, State);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Resp, sizeof (Resp), "rpmb-%a failed: %r",
                 State ? "unlock" : "lock", Status);
    FastbootFail (Resp);
    return;
  }

  AsciiSPrint (Resp, sizeof (Resp), "rpmb-%a requested; reboot required",
               State ? "unlock" : "lock");
  FastbootOkay (Resp);
}

STATIC VOID
CmdOemRpmbLock (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  CmdOemRpmbSetUnlockState (Arg, FALSE);
}

STATIC VOID
CmdOemRpmbUnlock (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  CmdOemRpmbSetUnlockState (Arg, TRUE);
}

/*
 * LocatePartition — resolve BlockIo + Handle for a bare partition
 * root name (e.g. "recovery", "dtbo").  On a multi-slot device the active-slot
 * suffixed name is tried first (_a or _b); the bare name is the fallback for
 * partitions that have no A/B variants.
 *
 * On success *BlockIoOut and *HandleOut are set; the resolved CHAR16 name is
 * written to ResolvedName (caller provides MAX_GPT_NAME_SIZE chars).
 */
STATIC EFI_STATUS
LocatePartition (
  IN  CONST CHAR8          *RootNameAscii,
  OUT EFI_BLOCK_IO_PROTOCOL **BlockIoOut,
  OUT EFI_HANDLE           **HandleOut,
  OUT CHAR16               *ResolvedName,
  IN  UINT32               ResolvedNameSize
  )
{
  EFI_STATUS   Status;
  CHAR16       SlottedName[MAX_GPT_NAME_SIZE];
  Slot         CurrentSlot;

  /* Build slot-suffixed candidate */
  AsciiStrToUnicodeStr (RootNameAscii, SlottedName);
  CurrentSlot = GetCurrentSlotSuffix ();
  StrnCatS (SlottedName, ARRAY_SIZE (SlottedName),
            CurrentSlot.Suffix, StrLen (CurrentSlot.Suffix));

  Status = PartitionGetInfo (SlottedName, BlockIoOut, HandleOut);
  if (Status == EFI_SUCCESS && *BlockIoOut != NULL) {
    StrnCpyS (ResolvedName, ResolvedNameSize, SlottedName, StrLen (SlottedName));
    return EFI_SUCCESS;
  }

  /* Fallback: bare (non-A/B) name */
  AsciiStrToUnicodeStr (RootNameAscii, ResolvedName);
  Status = PartitionGetInfo (ResolvedName, BlockIoOut, HandleOut);
  return Status;
}

/* -------------------------------------------------------------------------
 * getvar vbmeta:* handlers
 *
 * SHA256 is provided by AvbLib (linked transitively via BootLib → AvbLib).
 * Forward-declare the avb_sha256 context and functions to avoid the
 * AVB_INSIDE_LIBAVB_H include-guard problem.
 * -------------------------------------------------------------------------
 */
#define GBL_AVB_SHA256_DIGEST_SIZE  32
#define GBL_AVB_SHA256_BLOCK_SIZE   64

typedef struct {
  UINT32  h[8];
  UINT32  tot_len;
  UINT32  len;
  UINT8   block[2 * GBL_AVB_SHA256_BLOCK_SIZE];
  UINT8   buf[GBL_AVB_SHA256_DIGEST_SIZE];
  VOID   *user_data;
} GBL_AvbSHA256Ctx;

VOID   avb_sha256_init   (VOID *ctx);
VOID   avb_sha256_update (VOID *ctx, CONST UINT8 *data, UINT32 len);
UINT8 *avb_sha256_final  (VOID *ctx);

#define GBL_VBMETA_SHA256_LEN  GBL_AVB_SHA256_DIGEST_SIZE
#define GBL_VBMETA_HEX_LEN    65   /* 32*2 + NUL */

/* Descriptor type tag for vbmeta lookup helpers (defined below). */
typedef enum {
  GblPartDescNone  = 0,
  GblPartDescHash  = 1,
  GblPartDescChain = 2,
} GBL_PART_DESC_TYPE;

/* Forward decls — implementations are below the synthesize function. */
STATIC EFI_STATUS
GblVbmetaReadActiveSlot (
  OUT UINT8                  **BufOut,
  OUT UINT64                  *SizeOut
  );

STATIC EFI_STATUS
GblVbmetaLookupDescriptor (
  IN  CONST UINT8            *VbmBuf,
  IN  UINT64                  VbmSize,
  IN  CONST CHAR8            *PartName,
  OUT GBL_PART_DESC_TYPE     *TypeOut,
  OUT CONST UINT8           **DigestOut,
  OUT UINT32                 *DigestLenOut,
  OUT CONST UINT8           **PubKeyOut,
  OUT UINT32                 *PubKeyLenOut
  );

/* -------------------------------------------------------------------------
 * `oem synthesize-and-flash <partition> [commit]`
 *
 * Builds a fake-signed AVB0 vbmeta whose embedded AvbRSAPublicKey is the
 * device's REAL OEM pubkey, extracted at runtime from the chain partition
 * descriptor in on-disk vbmeta_<active_slot>.  Hash descriptor's
 * expected_digest = SHA-256 of the staged content.  Writes
 *   staged || zero-pad-to-4KiB || vbmeta (header+auth+aux) || zero-pad || AvbFooter
 * to the on-disk partition.  Dry-run by default; literal "commit" writes.
 *
 * Algorithm + auth/aux sizing is derived from the chain descriptor's
 * pk_len so the encoding adapts to whatever OEM key size is in use:
 *   pk_len=520  → SHA256_RSA2048 (sig=256, auth=320)
 *   pk_len=1032 → SHA256_RSA4096 (sig=512, auth=576)
 *   pk_len=2056 → SHA256_RSA8192 (sig=1024, auth=1088)
 *
 * libavb at parse time sees a stock-shaped vbmeta:
 *   - sizes valid, 64-byte aligned
 *   - algorithm_type matches what stock recovery uses
 *   - auth.hash = real SHA256(header || aux), so HASH_MISMATCH not hit
 *   - signature is zero → RSA verify fails → SIGNATURE_MISMATCH
 *   - chain-pubkey callback finds our pubkey bytes MATCH chain desc's
 *     expected bytes (we copied them) → no PUBLIC_KEY_REJECTED
 *
 * SIGNATURE_MISMATCH propagates as AVB_SLOT_VERIFY_RESULT_ERROR_VERIFICATION
 * (recoverable per result_should_continue).  patch10 catches it.
 *
 * No partition whitelist — user picks the partition; the host-side fastboot
 * non-HLOS safety hook gates abuse.  Replaces the prior `oem graft-and-flash`.
 *
 * Workflow:
 *   fastboot stage <new-image>.img
 *   fastboot oem synthesize-and-flash recovery           # dry-run
 *   fastboot oem synthesize-and-flash recovery commit    # write
 * ------------------------------------------------------------------------- */

STATIC VOID
SynthBe32 (UINT8 *Buf, UINT32 V)
{
  Buf[0] = (UINT8)(V >> 24);
  Buf[1] = (UINT8)(V >> 16);
  Buf[2] = (UINT8)(V >> 8);
  Buf[3] = (UINT8)V;
}

STATIC VOID
SynthBe64 (UINT8 *Buf, UINT64 V)
{
  SynthBe32 (Buf,     (UINT32)(V >> 32));
  SynthBe32 (Buf + 4, (UINT32)V);
}

STATIC UINT32
GblBe32 (CONST UINT8 *Buf)
{
  return ((UINT32)Buf[0] << 24) | ((UINT32)Buf[1] << 16) |
         ((UINT32)Buf[2] << 8)  |  (UINT32)Buf[3];
}

STATIC UINT64
GblBe64 (CONST UINT8 *Buf)
{
  return ((UINT64)GblBe32 (Buf) << 32) | (UINT64)GblBe32 (Buf + 4);
}

/* -------------------------------------------------------------------------
 * GblSynthesizeAndCommit — core synthesizer used by both
 * CmdOemSynthesizeAndFlash (staged content) and CmdOemFixVbmetaFooter
 * (on-disk content).  Locates the partition, reads vbmeta_<slot>, looks
 * up the chain descriptor for PartName, derives algorithm + sizing from
 * the OEM pubkey length, builds the synthesized vbmeta, and writes it
 * back along with the supplied Content.
 *
 *   DoCommit=FALSE: dry-run, prints planned layout via FastbootInfo
 *                   (if EmitFastbootInfo); does not touch the partition.
 *   DoCommit=TRUE : full write.
 *
 * On EFI_SUCCESS, RespOut holds a final status line for FastbootOkay.
 * On error,       RespOut holds a human-readable failure reason for
 *                 FastbootFail.  The caller decides which to emit.
 * ------------------------------------------------------------------------- */
STATIC EFI_STATUS
GblSynthesizeAndCommit (
  IN CONST CHAR8 *PartName,
  IN UINT32       NameLen,
  IN CONST UINT8 *Content,
  IN UINT64       ContentSize,
  IN BOOLEAN      DoCommit,
  IN BOOLEAN      EmitFastbootInfo,
  OUT CHAR8      *RespOut,
  IN UINTN        RespCap
  )
{
  EFI_STATUS              Status;
  CHAR8                   Info[MAX_RSP_SIZE];

  EFI_BLOCK_IO_PROTOCOL  *BlockIo = NULL;
  EFI_HANDLE             *Handle  = NULL;
  CHAR16                  ResolvedName[MAX_GPT_NAME_SIZE];
  UINT64                  PartSize = 0;
  UINT8                  *Assembly = NULL;

  GBL_AvbSHA256Ctx        Ctx;
  UINT8                  *DigestPtr;
  UINT8                   ContentDigest[GBL_AVB_SHA256_DIGEST_SIZE];

  UINT8                  *VbmBuf  = NULL;
  UINT64                  VbmSize = 0;
  GBL_PART_DESC_TYPE      DescType;
  CONST UINT8            *DescDigest    = NULL;
  UINT32                  DescDigestLen = 0;
  CONST UINT8            *ChainPubKey   = NULL;
  UINT32                  ChainPubKeyLen = 0;
  UINT32                  ChainKeyBits   = 0;
  CONST CHAR8            *AlgoName;

  UINT64                  DescUnpaddedLen, DescPad, DescSize;
  UINT64                  AuxUnpad, AuxPad, AuxSize;
  UINT64                  AuthUnpad, AuthPad, AuthSize;
  UINT64                  PkOffset, PkSize;
  UINT32                  AlgorithmType;
  UINT64                  SigSize;
  UINT64                  VbmetaSize, VbmetaOffset;
  UINT64                  AuxOffsetInVbmeta;
  UINT8                  *Vp, *Ap, *Xp, *Dp, *Kp, *Fp;
  CONST CHAR8            *Release  = "gbl-synthesize 1.2";
  CONST CHAR8            *HashAlgo = "sha256";
  UINT32                  RelLen;

  CONST UINT64 GBL_SYNTH_HASH_LEN  = GBL_AVB_SHA256_DIGEST_SIZE; /* 32 */
  CONST UINT64 GBL_AVB_BLOCK_ALIGN = 64;

  if (Content == NULL || ContentSize == 0) {
    AsciiSPrint (RespOut, RespCap, "no content supplied to synthesize");
    return EFI_INVALID_PARAMETER;
  }

  /* ---- locate target partition ---- */
  Status = LocatePartition (PartName, &BlockIo, &Handle,
                            ResolvedName, ARRAY_SIZE (ResolvedName));
  if (EFI_ERROR (Status) || BlockIo == NULL) {
    AsciiSPrint (RespOut, RespCap,
                 "partition '%a' not found: %r", PartName, Status);
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;
  }
  PartSize = GetPartitionSize (BlockIo);
  if (PartSize == 0) {
    AsciiSPrint (RespOut, RespCap, "could not determine partition size");
    return EFI_DEVICE_ERROR;
  }

  /* ---- read vbmeta_<slot> + look up chain desc for target ---- */
  Status = GblVbmetaReadActiveSlot (&VbmBuf, &VbmSize);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (RespOut, RespCap, "vbmeta_<slot> read failed: %r", Status);
    return Status;
  }

  Status = GblVbmetaLookupDescriptor (VbmBuf, VbmSize, PartName,
                                      &DescType, &DescDigest, &DescDigestLen,
                                      &ChainPubKey, &ChainPubKeyLen);
  if (EFI_ERROR (Status) || DescType != GblPartDescChain ||
      ChainPubKey == NULL || ChainPubKeyLen == 0) {
    AsciiSPrint (RespOut, RespCap,
                 "no chain partition descriptor for '%a' in vbmeta_<slot>", PartName);
    FreePool (VbmBuf);
    return EFI_NOT_FOUND;
  }

  /* Derive algorithm + signature size from chain pk_len. */
  switch (ChainPubKeyLen) {
  case 520:                                       /* 4 + 4 + 256 + 256 */
    AlgorithmType = 1;                            /* SHA256_RSA2048 */
    SigSize       = 256;
    ChainKeyBits  = 2048;
    AlgoName      = "SHA256_RSA2048";
    break;
  case 1032:                                      /* 4 + 4 + 512 + 512 */
    AlgorithmType = 2;                            /* SHA256_RSA4096 */
    SigSize       = 512;
    ChainKeyBits  = 4096;
    AlgoName      = "SHA256_RSA4096";
    break;
  case 2056:                                      /* 4 + 4 + 1024 + 1024 */
    AlgorithmType = 3;                            /* SHA256_RSA8192 */
    SigSize       = 1024;
    ChainKeyBits  = 8192;
    AlgoName      = "SHA256_RSA8192";
    break;
  default:
    AsciiSPrint (RespOut, RespCap,
                 "unsupported chain pk_len=%u for '%a' (expected 520/1032/2056)",
                 ChainPubKeyLen, PartName);
    FreePool (VbmBuf);
    return EFI_UNSUPPORTED;
  }

  /* ---- compute layout sizes ---- */
  DescUnpaddedLen = 16U + 116U + (UINT64)NameLen + GBL_SYNTH_HASH_LEN;
  DescPad         = (0ULL - DescUnpaddedLen) & 7ULL;
  DescSize        = DescUnpaddedLen + DescPad;

  PkOffset  = DescSize;
  PkSize    = ChainPubKeyLen;
  AuxUnpad  = DescSize + (UINT64)ChainPubKeyLen;
  AuthUnpad = GBL_SYNTH_HASH_LEN + SigSize;
  AuxPad    = (0ULL - AuxUnpad)  & (GBL_AVB_BLOCK_ALIGN - 1ULL);
  AuxSize   = AuxUnpad + AuxPad;
  AuthPad   = (0ULL - AuthUnpad) & (GBL_AVB_BLOCK_ALIGN - 1ULL);
  AuthSize  = AuthUnpad + AuthPad;

  VbmetaSize        = (UINT64)GBL_AVB_VBMETA_HEADER_SIZE + AuthSize + AuxSize;
  VbmetaOffset      = (ContentSize + 4095ULL) & ~4095ULL;
  AuxOffsetInVbmeta = (UINT64)GBL_AVB_VBMETA_HEADER_SIZE + AuthSize;

  if (VbmetaOffset + VbmetaSize + GBL_AVB_FOOTER_SIZE > PartSize) {
    AsciiSPrint (RespOut, RespCap,
                 "partition too small: content=%llu + pad + vbmeta=%llu + footer=64 > %llu",
                 ContentSize, VbmetaSize, PartSize);
    FreePool (VbmBuf);
    return EFI_BUFFER_TOO_SMALL;
  }

  /* ---- compute SHA256(content) for descriptor.expected_digest ---- */
  avb_sha256_init (&Ctx);
  avb_sha256_update (&Ctx, Content, (UINT32)ContentSize);
  DigestPtr = avb_sha256_final (&Ctx);
  CopyMem (ContentDigest, DigestPtr, GBL_AVB_SHA256_DIGEST_SIZE);

  /* ---- info ---- */
  if (EmitFastbootInfo) {
    AsciiSPrint (Info, sizeof (Info),
                 "target=%a part=%llu B  content=%llu B  algo=%a (RSA-%u)",
                 PartName, PartSize, ContentSize, AlgoName, ChainKeyBits);
    FastbootInfo (Info);
    WaitForTransferComplete ();

    AsciiSPrint (Info, sizeof (Info),
                 "vbm_off=%llu vbm_sz=%llu auth=%llu aux=%llu pk=%u",
                 VbmetaOffset, VbmetaSize, AuthSize, AuxSize, ChainPubKeyLen);
    FastbootInfo (Info);
    WaitForTransferComplete ();

    AsciiSPrint (Info, sizeof (Info),
                 "digest = SHA256(content) %02x%02x%02x%02x...%02x%02x%02x%02x",
                 ContentDigest[0], ContentDigest[1], ContentDigest[2], ContentDigest[3],
                 ContentDigest[28], ContentDigest[29], ContentDigest[30], ContentDigest[31]);
    FastbootInfo (Info);
    WaitForTransferComplete ();

    AsciiSPrint (Info, sizeof (Info),
                 "OEM pubkey %02x%02x%02x%02x...%02x%02x%02x%02x (chain[%a])",
                 ChainPubKey[8],  ChainPubKey[9],  ChainPubKey[10], ChainPubKey[11],
                 ChainPubKey[8 + (ChainKeyBits / 8) - 4], ChainPubKey[8 + (ChainKeyBits / 8) - 3],
                 ChainPubKey[8 + (ChainKeyBits / 8) - 2], ChainPubKey[8 + (ChainKeyBits / 8) - 1],
                 PartName);
    FastbootInfo (Info);
    WaitForTransferComplete ();
  }

  if (!DoCommit) {
    AsciiSPrint (RespOut, RespCap,
                 "DRY RUN -- re-issue with 'commit' to write");
    FreePool (VbmBuf);
    return EFI_SUCCESS;
  }

  /* ---- assemble partition-sized buffer ---- */
  Assembly = AllocateZeroPool (PartSize);
  if (Assembly == NULL) {
    AsciiSPrint (RespOut, RespCap, "alloc failed for partition-sized buffer");
    FreePool (VbmBuf);
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (Assembly, Content, ContentSize);

  Vp = Assembly + VbmetaOffset;
  Ap = Vp + GBL_AVB_VBMETA_HEADER_SIZE;
  Xp = Vp + AuxOffsetInVbmeta;

  /* ---- AvbVBMetaImageHeader ---- */
  CopyMem    (Vp,        GBL_AVB_VBMETA_MAGIC, 4);
  SynthBe32 (Vp + 0x04, 1);
  SynthBe32 (Vp + 0x08, 0);
  SynthBe64 (Vp + 0x0C, AuthSize);
  SynthBe64 (Vp + 0x14, AuxSize);
  SynthBe32 (Vp + 0x1C, AlgorithmType);
  SynthBe64 (Vp + 0x20, 0);                          /* hash_offset */
  SynthBe64 (Vp + 0x28, GBL_SYNTH_HASH_LEN);         /* hash_size */
  SynthBe64 (Vp + 0x30, GBL_SYNTH_HASH_LEN);         /* signature_offset */
  SynthBe64 (Vp + 0x38, SigSize);                    /* signature_size */
  SynthBe64 (Vp + 0x40, PkOffset);                   /* public_key_offset */
  SynthBe64 (Vp + 0x48, PkSize);                     /* public_key_size */
  SynthBe64 (Vp + 0x60, 0);                          /* descriptors_offset */
  SynthBe64 (Vp + 0x68, DescSize);                   /* descriptors_size */
  SynthBe64 (Vp + 0x70, 0);
  SynthBe32 (Vp + 0x78, 0);
  SynthBe32 (Vp + 0x7C, 0);
  RelLen = (UINT32)AsciiStrLen (Release);
  if (RelLen > 48) RelLen = 48;
  CopyMem (Vp + 0x80, Release, RelLen);

  /* ---- aux: AvbHashDescriptor ---- */
  Dp = Xp;
  SynthBe64 (Dp + 0x00, 2);
  SynthBe64 (Dp + 0x08, DescSize - 16ULL);
  SynthBe64 (Dp + 16U + 0x00, ContentSize);
  CopyMem    (Dp + 16U + 0x08, HashAlgo, AsciiStrLen (HashAlgo));
  SynthBe32 (Dp + 16U + 0x28, NameLen);
  SynthBe32 (Dp + 16U + 0x2C, 0);
  SynthBe32 (Dp + 16U + 0x30, (UINT32)GBL_SYNTH_HASH_LEN);
  SynthBe32 (Dp + 16U + 0x34, 0);
  CopyMem (Dp + 132U,           PartName,      NameLen);
  CopyMem (Dp + 132U + NameLen, ContentDigest, GBL_AVB_SHA256_DIGEST_SIZE);

  /* ---- aux: real OEM AvbRSAPublicKey ---- */
  Kp = Xp + PkOffset;
  CopyMem (Kp, ChainPubKey, ChainPubKeyLen);

  /* ---- auth: hash + zero signature ---- */
  avb_sha256_init   (&Ctx);
  avb_sha256_update (&Ctx, Vp, (UINT32)GBL_AVB_VBMETA_HEADER_SIZE);
  avb_sha256_update (&Ctx, Xp, (UINT32)AuxSize);
  DigestPtr = avb_sha256_final (&Ctx);
  CopyMem (Ap, DigestPtr, GBL_SYNTH_HASH_LEN);
  /* signature[SigSize] + padding stay zero from AllocateZeroPool */

  /* ---- AvbFooter ---- */
  Fp = Assembly + PartSize - GBL_AVB_FOOTER_SIZE;
  CopyMem    (Fp,         GBL_AVB_FOOTER_MAGIC, 4);
  SynthBe32 (Fp + 0x04,  1);
  SynthBe32 (Fp + 0x08,  0);
  SynthBe64 (Fp + 0x0C,  ContentSize);
  SynthBe64 (Fp + 0x14,  VbmetaOffset);
  SynthBe64 (Fp + 0x1C,  VbmetaSize);

  FreePool (VbmBuf);
  VbmBuf = NULL;

  Status = WriteBlockToPartition (BlockIo, Handle, 0, PartSize, Assembly);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (RespOut, RespCap, "WriteBlockToPartition failed: %r", Status);
    FreePool (Assembly);
    return Status;
  }
  FreePool (Assembly);

  AsciiSPrint (RespOut, RespCap,
               "synthesized %s: wrote %llu B (content=%llu B, vbmeta @ %llu B, %a)",
               ResolvedName, PartSize, ContentSize, VbmetaOffset, AlgoName);
  return EFI_SUCCESS;
}

STATIC VOID
CmdOemSynthesizeAndFlash (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  CHAR8         Resp[MAX_RSP_SIZE];
  CHAR8         PartName[MAX_GPT_NAME_SIZE];
  CONST CHAR8  *ArgPtr;
  CONST CHAR8  *Rest;
  UINT32        NameLen;
  BOOLEAN       DoCommit = FALSE;
  EFI_STATUS    Status;

  /* ---- exchange staging buffer ---- */
  ExchangeFlashAndUsbDataBuf ();
  if (mFlashDataBuffer == NULL || mFlashNumDataBytes == 0) {
    FastbootFail ("no staged image -- run `fastboot stage <file>` first");
    return;
  }

  /* ---- parse args: "<partition> [commit]" ---- */
  ArgPtr = Arg;
  while (*ArgPtr == ' ') ArgPtr++;
  NameLen = 0;
  while (ArgPtr[NameLen] != '\0' && ArgPtr[NameLen] != ' ' &&
         NameLen < sizeof (PartName) - 1) {
    NameLen++;
  }
  if (NameLen == 0) {
    FastbootFail ("usage: oem synthesize-and-flash <partition> [commit]");
    return;
  }
  AsciiStrnCpyS (PartName, sizeof (PartName), ArgPtr, NameLen);
  Rest = ArgPtr + NameLen;
  while (*Rest == ' ') Rest++;
  if (AsciiStrCmp (Rest, "commit") == 0) {
    DoCommit = TRUE;
  } else if (*Rest != '\0') {
    FastbootFail ("unknown trailing token (expected 'commit' or empty)");
    return;
  }

  Status = GblSynthesizeAndCommit (PartName, NameLen,
                                   mFlashDataBuffer, (UINT64)mFlashNumDataBytes,
                                   DoCommit,
                                   TRUE,                          /* EmitFastbootInfo */
                                   Resp, sizeof (Resp));
  if (EFI_ERROR (Status)) {
    FastbootFail (Resp);
  } else {
    FastbootOkay (Resp);
  }
}

/* -------------------------------------------------------------------------
 * `oem fix-vbmeta-footer <partition> [commit]`
 *
 * Same effect as `synthesize-and-flash`, but reads the partition's CURRENT
 * on-disk content instead of using a staged image.  Determines the content
 * size by reading the partition's existing AvbFooter (at end - 64) and
 * pulling its `original_image_size` field.
 *
 * Pre-flight sanity checks (refuse if):
 *   - no AvbFooter (magic mismatch at end - 64) — can't determine where image
 *     content ends; user should re-flash the recovery image first
 *   - existing vbmeta release_string starts with "gbl-synthesize" — already
 *     fixed by us, nothing to do
 *
 * If the existing vbmeta carries `algorithm_type != 0` AND its pubkey matches
 * what main vbmeta's chain descriptor expects (i.e. genuinely OEM-signed),
 * we emit a warning but still let the commit proceed — useful for forcing a
 * re-synthesis after an OTA.  The user can stop at the dry-run stage if they
 * weren't expecting that state.
 *
 * Workflow:
 *   fastboot flash recovery <new-image>.img        # vendor flow, no AVB fix
 *   fastboot oem fix-vbmeta-footer recovery        # dry-run
 *   fastboot oem fix-vbmeta-footer recovery commit # write
 * ------------------------------------------------------------------------- */
STATIC VOID
CmdOemFixVbmetaFooter (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  EFI_STATUS              Status;
  CHAR8                   Resp[MAX_RSP_SIZE];
  CHAR8                   PartName[MAX_GPT_NAME_SIZE];
  CONST CHAR8            *ArgPtr;
  CONST CHAR8            *Rest;
  UINT32                  NameLen;
  BOOLEAN                 DoCommit = FALSE;

  EFI_BLOCK_IO_PROTOCOL  *BlockIo  = NULL;
  EFI_HANDLE             *Handle   = NULL;
  CHAR16                  ResolvedName[MAX_GPT_NAME_SIZE];
  UINT64                  PartSize  = 0;
  UINT64                  BlockSize;
  UINT64                  ReadBytes;
  UINT8                  *PartBuf  = NULL;

  CONST UINT8            *FooterPtr;
  UINT64                  FooterOrigImgSize;
  UINT64                  FooterVbmOff;
  UINT64                  FooterVbmSize;
  CONST UINT8            *ExistingHdr;
  UINT32                  ExistingAlgo = 0;
  CONST CHAR8            *ExistingRelease;

  /* ---- parse args: "<partition> [commit]" ---- */
  ArgPtr = Arg;
  while (*ArgPtr == ' ') ArgPtr++;
  NameLen = 0;
  while (ArgPtr[NameLen] != '\0' && ArgPtr[NameLen] != ' ' &&
         NameLen < sizeof (PartName) - 1) {
    NameLen++;
  }
  if (NameLen == 0) {
    FastbootFail ("usage: oem fix-vbmeta-footer <partition> [commit]");
    return;
  }
  AsciiStrnCpyS (PartName, sizeof (PartName), ArgPtr, NameLen);
  Rest = ArgPtr + NameLen;
  while (*Rest == ' ') Rest++;
  if (AsciiStrCmp (Rest, "commit") == 0) {
    DoCommit = TRUE;
  } else if (*Rest != '\0') {
    FastbootFail ("unknown trailing token (expected 'commit' or empty)");
    return;
  }

  /* ---- locate partition + size ---- */
  Status = LocatePartition (PartName, &BlockIo, &Handle,
                            ResolvedName, ARRAY_SIZE (ResolvedName));
  if (EFI_ERROR (Status) || BlockIo == NULL) {
    AsciiSPrint (Resp, sizeof (Resp),
                 "partition '%a' not found: %r", PartName, Status);
    FastbootFail (Resp);
    return;
  }
  PartSize = GetPartitionSize (BlockIo);
  if (PartSize < GBL_AVB_FOOTER_SIZE + GBL_AVB_VBMETA_HEADER_SIZE) {
    FastbootFail ("partition too small to hold a vbmeta footer");
    return;
  }

  /* ---- read entire partition (caller-allocated, partition-sized buffer) ---- */
  BlockSize = BlockIo->Media->BlockSize;
  ReadBytes = (PartSize + BlockSize - 1) & ~(BlockSize - 1);
  PartBuf   = AllocatePool (ReadBytes);
  if (PartBuf == NULL) {
    FastbootFail ("alloc failed for partition read");
    return;
  }
  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId,
                                0, ReadBytes, PartBuf);
  if (EFI_ERROR (Status)) {
    AsciiSPrint (Resp, sizeof (Resp), "partition read failed: %r", Status);
    FastbootFail (Resp);
    FreePool (PartBuf);
    return;
  }

  /* ---- parse existing AvbFooter (last 64 B) ---- */
  FooterPtr = PartBuf + (PartSize - GBL_AVB_FOOTER_SIZE);
  if (CompareMem (FooterPtr, GBL_AVB_FOOTER_MAGIC, 4) != 0) {
    AsciiSPrint (Resp, sizeof (Resp),
                 "no AvbFooter on '%a' (no 'AVBf' magic at end-64)", PartName);
    FastbootFail (Resp);
    FreePool (PartBuf);
    return;
  }
  FooterOrigImgSize = GblBe64 (FooterPtr + 0x0C);
  FooterVbmOff      = GblBe64 (FooterPtr + 0x14);
  FooterVbmSize     = GblBe64 (FooterPtr + 0x1C);

  if (FooterOrigImgSize == 0 || FooterOrigImgSize > PartSize) {
    AsciiSPrint (Resp, sizeof (Resp),
                 "AvbFooter.original_image_size=%llu invalid for part_size=%llu",
                 FooterOrigImgSize, PartSize);
    FastbootFail (Resp);
    FreePool (PartBuf);
    return;
  }
  if (FooterVbmOff + FooterVbmSize > PartSize - GBL_AVB_FOOTER_SIZE ||
      FooterVbmSize < GBL_AVB_VBMETA_HEADER_SIZE) {
    AsciiSPrint (Resp, sizeof (Resp),
                 "AvbFooter vbm_off/vbm_size invalid (off=%llu sz=%llu)",
                 FooterVbmOff, FooterVbmSize);
    FastbootFail (Resp);
    FreePool (PartBuf);
    return;
  }

  /* ---- sanity-check existing vbmeta ---- */
  ExistingHdr = PartBuf + FooterVbmOff;
  if (CompareMem (ExistingHdr, GBL_AVB_VBMETA_MAGIC, 4) != 0) {
    AsciiSPrint (Resp, sizeof (Resp),
                 "vbmeta at offset %llu has no AVB0 magic", FooterVbmOff);
    FastbootFail (Resp);
    FreePool (PartBuf);
    return;
  }
  ExistingAlgo    = (UINT32)GblBe32 (ExistingHdr + 0x1C);
  ExistingRelease = (CONST CHAR8 *)(ExistingHdr + 0x80);   /* 48-char field */

  /* Already gbl-synthesized → refuse unless commit (and even then warn). */
  if (AsciiStrnCmp (ExistingRelease, "gbl-synthesize", 14) == 0) {
    AsciiSPrint (Resp, sizeof (Resp),
                 "existing vbmeta is already gbl-synthesized (release=%a)",
                 ExistingRelease);
    FastbootInfo (Resp);
    WaitForTransferComplete ();
  }
  /* Stock-signed → warn but allow (user might be forcing re-synth after OTA). */
  if (ExistingAlgo != 0 &&
      AsciiStrnCmp (ExistingRelease, "gbl-synthesize", 14) != 0) {
    AsciiSPrint (Resp, sizeof (Resp),
                 "existing vbmeta looks stock-signed (algo=%u, release=%a) -- proceeding",
                 ExistingAlgo, ExistingRelease);
    FastbootInfo (Resp);
    WaitForTransferComplete ();
  }
  if (ExistingAlgo == 0) {
    AsciiSPrint (Resp, sizeof (Resp),
                 "existing vbmeta is unsigned (algo=NONE, release=%a)",
                 ExistingRelease);
    FastbootInfo (Resp);
    WaitForTransferComplete ();
  }

  AsciiSPrint (Resp, sizeof (Resp),
               "in-place: part=%llu B  content=%llu B (from existing footer)",
               PartSize, FooterOrigImgSize);
  FastbootInfo (Resp);
  WaitForTransferComplete ();

  /* ---- synthesize using existing content ---- */
  Status = GblSynthesizeAndCommit (PartName, NameLen,
                                   PartBuf, FooterOrigImgSize,
                                   DoCommit,
                                   TRUE,
                                   Resp, sizeof (Resp));
  FreePool (PartBuf);
  if (EFI_ERROR (Status)) {
    FastbootFail (Resp);
  } else {
    FastbootOkay (Resp);
  }
}

/* -------------------------------------------------------------------------
 * Menu action wrappers — invoked from MenuKeysDetection.c.  Non-STATIC so
 * the linker can resolve them from the BootLib menu dispatcher.  These run
 * while the fastboot menu UI is on-screen (no USB fastboot session active),
 * so they emit NO FastbootInfo packets — return value carries success/fail.
 * ------------------------------------------------------------------------- */
EFI_STATUS EFIAPI
GblMenuActionFixRecoveryVbmeta (VOID)
{
  EFI_STATUS              Status;
  CHAR8                   Resp[MAX_RSP_SIZE];
  CONST CHAR8            *PartName = "recovery";
  CONST UINT32            NameLen  = 8;

  EFI_BLOCK_IO_PROTOCOL  *BlockIo  = NULL;
  EFI_HANDLE             *Handle   = NULL;
  CHAR16                  ResolvedName[MAX_GPT_NAME_SIZE];
  UINT64                  PartSize  = 0;
  UINT64                  BlockSize, ReadBytes;
  UINT8                  *PartBuf   = NULL;
  CONST UINT8            *FooterPtr;
  UINT64                  FooterOrigImgSize;

  Status = LocatePartition (PartName, &BlockIo, &Handle,
                            ResolvedName, ARRAY_SIZE (ResolvedName));
  if (EFI_ERROR (Status) || BlockIo == NULL)
    return EFI_ERROR (Status) ? Status : EFI_NOT_FOUND;

  PartSize = GetPartitionSize (BlockIo);
  if (PartSize < GBL_AVB_FOOTER_SIZE + GBL_AVB_VBMETA_HEADER_SIZE)
    return EFI_VOLUME_CORRUPTED;

  BlockSize = BlockIo->Media->BlockSize;
  ReadBytes = (PartSize + BlockSize - 1) & ~(BlockSize - 1);
  PartBuf   = AllocatePool (ReadBytes);
  if (PartBuf == NULL)
    return EFI_OUT_OF_RESOURCES;

  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId,
                                0, ReadBytes, PartBuf);
  if (EFI_ERROR (Status)) {
    FreePool (PartBuf);
    return Status;
  }

  FooterPtr = PartBuf + (PartSize - GBL_AVB_FOOTER_SIZE);
  if (CompareMem (FooterPtr, GBL_AVB_FOOTER_MAGIC, 4) != 0) {
    FreePool (PartBuf);
    return EFI_VOLUME_CORRUPTED;
  }
  FooterOrigImgSize = GblBe64 (FooterPtr + 0x0C);
  if (FooterOrigImgSize == 0 || FooterOrigImgSize > PartSize) {
    FreePool (PartBuf);
    return EFI_VOLUME_CORRUPTED;
  }

  Status = GblSynthesizeAndCommit (PartName, NameLen,
                                   PartBuf, FooterOrigImgSize,
                                   TRUE,    /* DoCommit */
                                   FALSE,   /* EmitFastbootInfo */
                                   Resp, sizeof (Resp));
  FreePool (PartBuf);
  return Status;
}

EFI_STATUS EFIAPI
GblMenuActionEscape (VOID)
{
  return BootFlowChainLoad ();
}

/* GBL_PART_DESC_TYPE is forward-declared above the synthesize block. */

/*
 * GblFastbootInfoLong — emit Value across one or more FastbootInfo packets.
 * Does NOT send a trailing OKAY.  Use this to stream rows before a final
 * FastbootOkay("") or as building blocks for GblFastbootRespondLong.
 *
 * FastbootInfo prepends "INFO" automatically, so Chunk must NOT include it.
 * Each INFO packet carries at most MAX_RSP_SIZE - 4 (tag) - 1 (NUL) = 59 chars.
 */
STATIC VOID
GblFastbootInfoLong (IN CONST CHAR8 *Value)
{
  CHAR8       Chunk[MAX_RSP_SIZE];
  UINTN       Total      = AsciiStrLen (Value);
  UINTN       Pos        = 0;
  CONST UINTN ChunkBytes = MAX_RSP_SIZE - sizeof ("INFO") - 1;  /* = 59 */

  if (Total == 0) {
    FastbootInfo ("");
    WaitForTransferComplete ();
    return;
  }
  while (Pos < Total) {
    UINTN N = Total - Pos;
    if (N > ChunkBytes)
      N = ChunkBytes;
    CopyMem (Chunk, Value + Pos, N);
    Chunk[N] = '\0';
    FastbootInfo (Chunk);
    WaitForTransferComplete ();
    Pos += N;
  }
}

/*
 * GblFastbootRespondLong — emit Value across one or more FastbootInfo packets,
 * then close with FastbootOkay ("").  Use for variables whose value can exceed
 * MAX_RSP_SIZE - sizeof("OKAY") - 1 (= 59 bytes).
 */
STATIC VOID
GblFastbootRespondLong (IN CONST CHAR8 *Value)
{
  GblFastbootInfoLong (Value);
  FastbootOkay ("");
}

/*
 * GblVbmetaHexDump — write Len bytes as lowercase hex into Out.
 * Out must be at least Len*2+1 bytes.  Always NUL-terminates.
 */
STATIC VOID
GblVbmetaHexDump (CONST UINT8 *Bytes, UINT32 Len, CHAR8 *Out, UINTN OutCap)
{
  STATIC CONST CHAR8 HexChars[] = "0123456789abcdef";
  UINTN i;
  UINTN Pos = 0;

  for (i = 0; i < Len && (Pos + 2) < OutCap; i++) {
    Out[Pos++] = HexChars[(Bytes[i] >> 4) & 0xF];
    Out[Pos++] = HexChars[Bytes[i] & 0xF];
  }
  if (Pos < OutCap)
    Out[Pos] = '\0';
  else
    Out[OutCap - 1] = '\0';
}

/*
 * GblVbmetaReadActiveSlot — allocate and read the active-slot vbmeta partition.
 * Caller must FreePool(*BufOut) on success.
 */
STATIC EFI_STATUS
GblVbmetaReadActiveSlot (OUT UINT8 **BufOut, OUT UINT64 *SizeOut)
{
  EFI_STATUS              Status;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo = NULL;
  EFI_HANDLE             *Handle  = NULL;
  CHAR16                  ResolvedName[MAX_GPT_NAME_SIZE];
  UINT64                  PartSize;
  UINT64                  BlockSize;
  UINT64                  ReadBytes;
  UINT8                  *Buf;

  Status = LocatePartition ("vbmeta", &BlockIo, &Handle,
                                    ResolvedName, ARRAY_SIZE (ResolvedName));
  if (EFI_ERROR (Status) || BlockIo == NULL)
    return EFI_NOT_FOUND;

  PartSize = GetPartitionSize (BlockIo);
  if (PartSize == 0)
    return EFI_DEVICE_ERROR;

  BlockSize = BlockIo->Media->BlockSize;
  ReadBytes = (PartSize + BlockSize - 1) & ~(BlockSize - 1);

  Buf = AllocatePool (ReadBytes);
  if (Buf == NULL)
    return EFI_OUT_OF_RESOURCES;

  Status = BlockIo->ReadBlocks (BlockIo, BlockIo->Media->MediaId,
                                0, ReadBytes, Buf);
  if (EFI_ERROR (Status)) {
    FreePool (Buf);
    return Status;
  }

  *BufOut  = Buf;
  *SizeOut = PartSize;
  return EFI_SUCCESS;
}

/*
 * GblVbmetaLookupDescriptor — walk vbmeta aux descriptors for PartName.
 * On match sets TypeOut and digest or pubkey out-pointers.
 * All out-pointers reference memory inside VbmBuf; caller must not free them.
 */
STATIC EFI_STATUS
GblVbmetaLookupDescriptor (
  IN  CONST UINT8            *VbmBuf,
  IN  UINT64                  VbmSize,
  IN  CONST CHAR8            *PartName,
  OUT GBL_PART_DESC_TYPE     *TypeOut,
  OUT CONST UINT8           **DigestOut,
  OUT UINT32                 *DigestLenOut,
  OUT CONST UINT8           **PubKeyOut,
  OUT UINT32                 *PubKeyLenOut
  )
{
  EFI_STATUS              Status;
  GBL_AVB_VBMETA_HEADER   Hdr;
  CONST UINT8            *AuxBlock;
  UINT64                  AuxSize;
  UINT64                  AuxOff;
  UINT64                  Cursor = 0;
  GBL_AVB_DESCRIPTOR_TAG  Tag;
  CONST UINT8            *Desc;
  UINT64                  DescLen;
  CONST UINT8            *DName;
  UINT32                  DNameLen;
  UINT32                  PartNameLen;

  *TypeOut      = GblPartDescNone;
  *DigestOut    = NULL;
  *DigestLenOut = 0;
  *PubKeyOut    = NULL;
  *PubKeyLenOut = 0;

  if (VbmSize < GBL_AVB_VBMETA_HEADER_SIZE)
    return EFI_INVALID_PARAMETER;

  Status = AvbParse_VbmetaHeader (VbmBuf, VbmSize, &Hdr);
  if (EFI_ERROR (Status))
    return Status;

  /* Auxiliary block starts after the fixed header + auth block */
  AuxOff  = (UINT64)GBL_AVB_VBMETA_HEADER_SIZE + Hdr.AuthenticationDataBlockSize;
  AuxSize = Hdr.AuxiliaryDataBlockSize;

  if (AuxOff + AuxSize > VbmSize)
    return EFI_VOLUME_CORRUPTED;

  AuxBlock    = VbmBuf + AuxOff;
  PartNameLen = (UINT32)AsciiStrLen (PartName);

  while (TRUE) {
    Status = AvbParse_NextDescriptor (AuxBlock, AuxSize, &Cursor,
                                      &Tag, &Desc, &DescLen);
    if (EFI_ERROR (Status))
      break;  /* end of descriptors or error */

    if (Tag == GblAvbDescHashTag) {
      if (EFI_ERROR (AvbParse_HashDescriptor (Desc, DescLen,
                                              &DName, &DNameLen,
                                              DigestOut, DigestLenOut)))
        continue;
      if (DNameLen != PartNameLen)
        continue;
      /* DName is not NUL-terminated; compare byte-by-byte */
      if (CompareMem (DName, PartName, DNameLen) == 0) {
        *TypeOut = GblPartDescHash;
        return EFI_SUCCESS;
      }
      *DigestOut    = NULL;
      *DigestLenOut = 0;
    } else if (Tag == GblAvbDescChainPartitionTag) {
      if (EFI_ERROR (AvbParse_ChainPartitionDescriptor (Desc, DescLen,
                                                        &DName, &DNameLen,
                                                        PubKeyOut, PubKeyLenOut)))
        continue;
      if (DNameLen != PartNameLen)
        continue;
      if (CompareMem (DName, PartName, DNameLen) == 0) {
        *TypeOut = GblPartDescChain;
        return EFI_SUCCESS;
      }
      *PubKeyOut    = NULL;
      *PubKeyLenOut = 0;
    } else {
      /* Skip other descriptor types */

    }
  }

  return EFI_SUCCESS;  /* *TypeOut == GblPartDescNone → not found */
}

/* ---- per-variable formatters -------------------------------------------- */

STATIC VOID
GblGetVarVbmetaDigest (VOID)
{
  EFI_STATUS       Status;
  UINT8           *Buf  = NULL;
  UINT64           Size = 0;
  GBL_AvbSHA256Ctx Ctx;
  UINT8           *Digest;
  CHAR8            Long[GBL_VBMETA_HEX_LEN];  /* 65 bytes — fits 32-byte digest */

  Status = GblVbmetaReadActiveSlot (&Buf, &Size);
  if (EFI_ERROR (Status)) {
    FastbootOkay ("error");
    return;
  }

  avb_sha256_init (&Ctx);
  avb_sha256_update (&Ctx, Buf, (UINT32)Size);
  Digest = avb_sha256_final (&Ctx);
  GblVbmetaHexDump (Digest, GBL_VBMETA_SHA256_LEN, Long, sizeof (Long));
  FreePool (Buf);
  GblFastbootRespondLong (Long);
}

STATIC VOID
GblGetVarVbmetaSlot (OUT CHAR8 *Out, IN UINTN OutCap)
{
  Slot  CurrentSlot = GetCurrentSlotSuffix ();
  /* Suffix is e.g. L"_a"; skip the leading underscore */
  CHAR8 SlotAsc[MAX_SLOT_SUFFIX_SZ];

  UnicodeStrToAsciiStrS (CurrentSlot.Suffix, SlotAsc, sizeof (SlotAsc));
  /* SlotAsc[0] == '_', SlotAsc[1] == 'a'/'b', SlotAsc[2] == '\0' */
  if (SlotAsc[0] == '_' && SlotAsc[1] != '\0')
    AsciiStrnCpyS (Out, OutCap, SlotAsc + 1, OutCap - 1);
  else
    AsciiStrnCpyS (Out, OutCap, SlotAsc, OutCap - 1);
}

STATIC VOID
GblGetVarVbmetaPartStatus (IN CONST CHAR8 *PartName,
                            OUT CHAR8 *Out, IN UINTN OutCap)
{
  EFI_STATUS           Status;
  UINT8               *VbmBuf  = NULL;
  UINT64               VbmSize = 0;
  GBL_PART_DESC_TYPE   Type;
  CONST UINT8         *Digest    = NULL;
  UINT32               DigestLen = 0;
  CONST UINT8         *PubKey    = NULL;
  UINT32               PubKeyLen = 0;

  /* Read vbmeta */
  Status = GblVbmetaReadActiveSlot (&VbmBuf, &VbmSize);
  if (EFI_ERROR (Status)) {
    AsciiStrnCpyS (Out, OutCap, Status == EFI_NOT_FOUND ?
                   "n/a" : "error", OutCap - 1);
    return;
  }

  Status = GblVbmetaLookupDescriptor (VbmBuf, VbmSize, PartName,
                                      &Type, &Digest, &DigestLen,
                                      &PubKey, &PubKeyLen);
  if (EFI_ERROR (Status)) {
    FreePool (VbmBuf);
    AsciiStrnCpyS (Out, OutCap, "error", OutCap - 1);
    return;
  }

  if (Type == GblPartDescNone) {
    FreePool (VbmBuf);
    AsciiStrnCpyS (Out, OutCap, "unsigned", OutCap - 1);
    return;
  }

  if (Type == GblPartDescHash) {
    /* Compute SHA256 of the named partition and compare with stored digest */
    EFI_BLOCK_IO_PROTOCOL *PartBlk  = NULL;
    EFI_HANDLE            *PartHdl  = NULL;
    CHAR16                 PartResolved[MAX_GPT_NAME_SIZE];
    UINT8                 *PartBuf  = NULL;
    UINT64                 PartSize = 0;
    UINT64                 ReadBytes;
    GBL_AvbSHA256Ctx       Ctx;
    UINT8                 *Computed;
    CONST CHAR8           *Result = "ok";

    Status = LocatePartition (PartName, &PartBlk, &PartHdl,
                                      PartResolved, ARRAY_SIZE (PartResolved));
    if (EFI_ERROR (Status) || PartBlk == NULL) {
      FreePool (VbmBuf);
      AsciiStrnCpyS (Out, OutCap, "n/a", OutCap - 1);
      return;
    }

    PartSize = GetPartitionSize (PartBlk);
    if (PartSize == 0) {
      FreePool (VbmBuf);
      AsciiStrnCpyS (Out, OutCap, "error", OutCap - 1);
      return;
    }

    ReadBytes = (PartSize + PartBlk->Media->BlockSize - 1)
                & ~(PartBlk->Media->BlockSize - 1);
    PartBuf = AllocatePool (ReadBytes);
    if (PartBuf == NULL) {
      FreePool (VbmBuf);
      AsciiStrnCpyS (Out, OutCap, "error", OutCap - 1);
      return;
    }

    Status = PartBlk->ReadBlocks (PartBlk, PartBlk->Media->MediaId,
                                  0, ReadBytes, PartBuf);
    if (EFI_ERROR (Status)) {
      FreePool (PartBuf);
      FreePool (VbmBuf);
      AsciiStrnCpyS (Out, OutCap, "error", OutCap - 1);
      return;
    }

    avb_sha256_init (&Ctx);
    avb_sha256_update (&Ctx, PartBuf, (UINT32)PartSize);
    Computed = avb_sha256_final (&Ctx);
    FreePool (PartBuf);

    if (DigestLen != GBL_VBMETA_SHA256_LEN ||
        CompareMem (Computed, Digest, GBL_VBMETA_SHA256_LEN) != 0)
      Result = "mismatch";

    FreePool (VbmBuf);
    AsciiStrnCpyS (Out, OutCap, Result, OutCap - 1);
    return;
  }

  /* Chain partition: presence of footer indicates chained vbmeta */
  if (Type == GblPartDescChain) {
    EFI_BLOCK_IO_PROTOCOL *PartBlk  = NULL;
    EFI_HANDLE            *PartHdl  = NULL;
    CHAR16                 PartResolved[MAX_GPT_NAME_SIZE];
    UINT8                 *PartBuf  = NULL;
    UINT64                 PartSize = 0;
    UINT64                 ReadBytes;
    GBL_AVB_FOOTER         ChainFooter;

    Status = LocatePartition (PartName, &PartBlk, &PartHdl,
                                      PartResolved, ARRAY_SIZE (PartResolved));
    if (EFI_ERROR (Status) || PartBlk == NULL) {
      FreePool (VbmBuf);
      AsciiStrnCpyS (Out, OutCap, "n/a", OutCap - 1);
      return;
    }

    PartSize = GetPartitionSize (PartBlk);
    ReadBytes = (PartSize + PartBlk->Media->BlockSize - 1)
                & ~(PartBlk->Media->BlockSize - 1);
    PartBuf = AllocatePool (ReadBytes);
    if (PartBuf == NULL) {
      FreePool (VbmBuf);
      AsciiStrnCpyS (Out, OutCap, "error", OutCap - 1);
      return;
    }

    Status = PartBlk->ReadBlocks (PartBlk, PartBlk->Media->MediaId,
                                  0, ReadBytes, PartBuf);
    if (EFI_ERROR (Status)) {
      FreePool (PartBuf);
      FreePool (VbmBuf);
      AsciiStrnCpyS (Out, OutCap, "error", OutCap - 1);
      return;
    }

    if (EFI_ERROR (AvbParse_Footer (PartBuf, PartSize, &ChainFooter)))
      AsciiStrnCpyS (Out, OutCap, "unsigned", OutCap - 1);
    else
      AsciiStrnCpyS (Out, OutCap, "ok", OutCap - 1);

    FreePool (PartBuf);
    FreePool (VbmBuf);
    return;
  }

  FreePool (VbmBuf);
  AsciiStrnCpyS (Out, OutCap, "n/a", OutCap - 1);
}

STATIC VOID
GblGetVarVbmetaPartDescType (IN CONST CHAR8 *PartName,
                              OUT CHAR8 *Out, IN UINTN OutCap)
{
  EFI_STATUS          Status;
  UINT8              *VbmBuf  = NULL;
  UINT64              VbmSize = 0;
  GBL_PART_DESC_TYPE  Type;
  CONST UINT8        *Digest = NULL;  UINT32 DigestLen = 0;
  CONST UINT8        *PubKey = NULL;  UINT32 PubKeyLen = 0;

  Status = GblVbmetaReadActiveSlot (&VbmBuf, &VbmSize);
  if (EFI_ERROR (Status)) {
    AsciiStrnCpyS (Out, OutCap, "error", OutCap - 1);
    return;
  }

  Status = GblVbmetaLookupDescriptor (VbmBuf, VbmSize, PartName,
                                      &Type, &Digest, &DigestLen,
                                      &PubKey, &PubKeyLen);
  FreePool (VbmBuf);

  if (EFI_ERROR (Status)) {
    AsciiStrnCpyS (Out, OutCap, "error", OutCap - 1);
    return;
  }

  switch (Type) {
  case GblPartDescHash:  AsciiStrnCpyS (Out, OutCap, "hash",  OutCap - 1); break;
  case GblPartDescChain: AsciiStrnCpyS (Out, OutCap, "chain", OutCap - 1); break;
  default:               AsciiStrnCpyS (Out, OutCap, "none",  OutCap - 1); break;
  }
}

/**
 * GblGetVarVbmetaPartExpected - emit the expected value for a vbmeta descriptor.
 *
 * Hash-type descriptor:  emits the stored 32-byte hash digest as 64 hex chars.
 * Chain-type descriptor: emits a SHA-256 fingerprint of the chain pubkey bytes
 *                        as 64 hex chars (NOT the raw pubkey).  The raw pubkey
 *                        (~1 KB → ~2 KB hex → ~36 INFO chunks) saturated the
 *                        USB transfer pool on canoe and wedged fastboot.
 *
 * Both cases fit in 2 INFO chunks, matching the vbmeta:digest output size.
 */
STATIC VOID
GblGetVarVbmetaPartExpected (IN CONST CHAR8 *PartName)
{
  EFI_STATUS          Status;
  UINT8              *VbmBuf  = NULL;
  UINT64              VbmSize = 0;
  GBL_PART_DESC_TYPE  Type;
  CONST UINT8        *Digest = NULL;  UINT32 DigestLen = 0;
  CONST UINT8        *PubKey = NULL;  UINT32 PubKeyLen = 0;

  Status = GblVbmetaReadActiveSlot (&VbmBuf, &VbmSize);
  if (EFI_ERROR (Status)) {
    FastbootOkay ("error");
    return;
  }

  Status = GblVbmetaLookupDescriptor (VbmBuf, VbmSize, PartName,
                                      &Type, &Digest, &DigestLen,
                                      &PubKey, &PubKeyLen);
  if (EFI_ERROR (Status)) {
    FreePool (VbmBuf);
    FastbootOkay ("error");
    return;
  }

  if (Type == GblPartDescHash && Digest != NULL && DigestLen > 0) {
    CHAR8 Long[GBL_VBMETA_HEX_LEN];
    GblVbmetaHexDump (Digest, DigestLen, Long, sizeof (Long));
    FreePool (VbmBuf);
    GblFastbootRespondLong (Long);
  } else if (Type == GblPartDescChain && PubKey != NULL && PubKeyLen > 0) {
    /* Fingerprint the pubkey via SHA-256 to avoid a ~2KB hex dump that
     * saturates the USB transfer pool on canoe and wedges fastboot.
     * Output: 64 hex chars (32-byte digest), same as vbmeta:digest. */
    GBL_AvbSHA256Ctx Ctx;
    UINT8            Fingerprint[GBL_AVB_SHA256_DIGEST_SIZE];
    CHAR8            Long[GBL_VBMETA_HEX_LEN];
    UINT8           *FpPtr;

    avb_sha256_init   (&Ctx);
    avb_sha256_update (&Ctx, PubKey, PubKeyLen);
    FpPtr = avb_sha256_final (&Ctx);
    CopyMem (Fingerprint, FpPtr, GBL_AVB_SHA256_DIGEST_SIZE);
    GblVbmetaHexDump (Fingerprint, GBL_AVB_SHA256_DIGEST_SIZE, Long, sizeof (Long));
    FreePool (VbmBuf);
    GblFastbootRespondLong (Long);
  } else {
    FreePool (VbmBuf);
    FastbootOkay ("n/a");
  }
}

STATIC VOID
GblGetVarVbmetaPartComputed (IN CONST CHAR8 *PartName)
{
  EFI_STATUS          Status;
  UINT8              *VbmBuf  = NULL;
  UINT64              VbmSize = 0;
  GBL_PART_DESC_TYPE  Type;
  CONST UINT8        *Digest = NULL;  UINT32 DigestLen = 0;
  CONST UINT8        *PubKey = NULL;  UINT32 PubKeyLen = 0;

  Status = GblVbmetaReadActiveSlot (&VbmBuf, &VbmSize);
  if (EFI_ERROR (Status)) {
    FastbootOkay ("error");
    return;
  }

  Status = GblVbmetaLookupDescriptor (VbmBuf, VbmSize, PartName,
                                      &Type, &Digest, &DigestLen,
                                      &PubKey, &PubKeyLen);
  FreePool (VbmBuf);

  if (EFI_ERROR (Status)) {
    FastbootOkay ("error");
    return;
  }

  if (Type == GblPartDescHash) {
    /* Compute SHA256 over the partition content */
    EFI_BLOCK_IO_PROTOCOL *PartBlk  = NULL;
    EFI_HANDLE            *PartHdl  = NULL;
    CHAR16                 PartResolved[MAX_GPT_NAME_SIZE];
    UINT8                 *PartBuf  = NULL;
    UINT64                 PartSize = 0;
    UINT64                 ReadBytes;
    GBL_AvbSHA256Ctx       Ctx;
    UINT8                 *Computed;
    CHAR8                  Long[GBL_VBMETA_HEX_LEN];

    Status = LocatePartition (PartName, &PartBlk, &PartHdl,
                                      PartResolved, ARRAY_SIZE (PartResolved));
    if (EFI_ERROR (Status) || PartBlk == NULL) {
      FastbootOkay ("n/a");
      return;
    }

    PartSize  = GetPartitionSize (PartBlk);
    ReadBytes = (PartSize + PartBlk->Media->BlockSize - 1)
                & ~(PartBlk->Media->BlockSize - 1);
    PartBuf = AllocatePool (ReadBytes);
    if (PartBuf == NULL) {
      FastbootOkay ("error");
      return;
    }

    Status = PartBlk->ReadBlocks (PartBlk, PartBlk->Media->MediaId,
                                  0, ReadBytes, PartBuf);
    if (EFI_ERROR (Status)) {
      FreePool (PartBuf);
      FastbootOkay ("error");
      return;
    }

    avb_sha256_init (&Ctx);
    avb_sha256_update (&Ctx, PartBuf, (UINT32)PartSize);
    Computed = avb_sha256_final (&Ctx);
    FreePool (PartBuf);

    GblVbmetaHexDump (Computed, GBL_VBMETA_SHA256_LEN, Long, sizeof (Long));
    GblFastbootRespondLong (Long);
    return;
  }

  if (Type == GblPartDescChain) {
    /* For chain: report whether the chained partition has an AVB footer */
    EFI_BLOCK_IO_PROTOCOL *PartBlk  = NULL;
    EFI_HANDLE            *PartHdl  = NULL;
    CHAR16                 PartResolved[MAX_GPT_NAME_SIZE];
    UINT8                 *PartBuf  = NULL;
    UINT64                 PartSize = 0;
    UINT64                 ReadBytes;
    GBL_AVB_FOOTER         ChainFooter;

    Status = LocatePartition (PartName, &PartBlk, &PartHdl,
                                      PartResolved, ARRAY_SIZE (PartResolved));
    if (EFI_ERROR (Status) || PartBlk == NULL) {
      FastbootOkay ("n/a");
      return;
    }

    PartSize  = GetPartitionSize (PartBlk);
    ReadBytes = (PartSize + PartBlk->Media->BlockSize - 1)
                & ~(PartBlk->Media->BlockSize - 1);
    PartBuf = AllocatePool (ReadBytes);
    if (PartBuf == NULL) {
      FastbootOkay ("error");
      return;
    }

    Status = PartBlk->ReadBlocks (PartBlk, PartBlk->Media->MediaId,
                                  0, ReadBytes, PartBuf);
    if (EFI_ERROR (Status)) {
      FreePool (PartBuf);
      FastbootOkay ("error");
      return;
    }

    if (EFI_ERROR (AvbParse_Footer (PartBuf, PartSize, &ChainFooter)))
      FastbootOkay ("unsigned");
    else
      FastbootOkay ("ok");

    FreePool (PartBuf);
    return;
  }

  FastbootOkay ("n/a");
}

/*
 * GblCmdGetVarVbmeta — called from CmdGetVar when Arg starts with "vbmeta:".
 * Returns TRUE if the variable was handled, FALSE otherwise.
 *
 * Long-value variables (digest, expected, computed) call GblFastbootRespondLong
 * internally and do not use the Resp buffer — the dispatcher must NOT call
 * FastbootOkay() again for those paths.
 * Short-value variables (slot, status, descriptor-type) fill Resp and the
 * dispatcher calls FastbootOkay(Resp).
 */
STATIC BOOLEAN
GblCmdGetVarVbmeta (IN CONST CHAR8 *Arg)
{
  CHAR8 Resp[MAX_RSP_SIZE];

  Resp[0] = '\0';

  if (AsciiStrCmp (Arg, "vbmeta:digest") == 0) {
    GblGetVarVbmetaDigest ();   /* sends INFO+OKAY internally */
    return TRUE;
  }

  if (AsciiStrCmp (Arg, "vbmeta:slot") == 0) {
    GblGetVarVbmetaSlot (Resp, sizeof (Resp));
    FastbootOkay (Resp);
    return TRUE;
  }

  /* vbmeta:<part>:<field> */
  {
    /* Find first ':' after "vbmeta:" */
    CONST CHAR8 *After  = Arg + AsciiStrLen ("vbmeta:");
    CONST CHAR8 *Colon2 = AsciiStrStr (After, ":");
    CONST CHAR8 *Field;
    CHAR8        PartName[48];
    UINT32       PNLen;

    if (Colon2 == NULL)
      return FALSE;

    PNLen = (UINT32)(Colon2 - After);
    if (PNLen == 0 || PNLen >= sizeof (PartName))
      return FALSE;

    AsciiStrnCpyS (PartName, sizeof (PartName), After, PNLen);

    Field = Colon2 + 1;

    if (AsciiStrCmp (Field, "status") == 0) {
      GblGetVarVbmetaPartStatus (PartName, Resp, sizeof (Resp));
      FastbootOkay (Resp);
      return TRUE;
    }
    if (AsciiStrCmp (Field, "descriptor-type") == 0) {
      GblGetVarVbmetaPartDescType (PartName, Resp, sizeof (Resp));
      FastbootOkay (Resp);
      return TRUE;
    }
    if (AsciiStrCmp (Field, "expected") == 0) {
      GblGetVarVbmetaPartExpected (PartName);  /* sends INFO+OKAY internally */
      return TRUE;
    }
    if (AsciiStrCmp (Field, "computed") == 0) {
      GblGetVarVbmetaPartComputed (PartName);  /* sends INFO+OKAY internally */
      return TRUE;
    }
  }

  return FALSE;
}

/*
 * CmdOemVbmetaStatus — `fastboot oem vbmeta-status`
 *
 * Compact one-INFO-packet-per-row table:
 *   partition(15) descriptor(13) status   (~40 chars, fits one INFO packet)
 *
 * Full expected/computed hex is available via:
 *   getvar vbmeta:<part>:expected|computed
 *
 * After the per-partition table: vbmeta digest (sha256) and flags.
 *
 * Known partitions iterated:
 *   boot, init_boot, vendor_boot, recovery, dtbo,
 *   vbmeta_system, vbmeta_vendor
 *
 * Partitions with no descriptor in vbmeta are silently skipped.
 */
STATIC VOID
CmdOemVbmetaStatus (IN CONST CHAR8 *Arg, IN VOID *Data, IN UINT32 Size)
{
  /* Suppress unused-parameter warnings */
  (VOID)Arg;
  (VOID)Data;
  (VOID)Size;

  static CONST CHAR8 *KnownParts[] = {
    "boot", "init_boot", "vendor_boot", "recovery",
    "dtbo", "vbmeta_system", "vbmeta_vendor",
  };
  CONST UINTN NumKnown = sizeof (KnownParts) / sizeof (KnownParts[0]);

  EFI_STATUS         Status;
  UINT8             *VbmBuf  = NULL;
  UINT64             VbmSize = 0;
  CHAR8              Line[128];
  UINTN              Idx;

  /* ---- read vbmeta once ------------------------------------------------- */
  Status = GblVbmetaReadActiveSlot (&VbmBuf, &VbmSize);
  if (EFI_ERROR (Status)) {
    FastbootFail ("vbmeta-status: cannot read vbmeta partition");
    return;
  }

  /* ---- header ----------------------------------------------------------- */
  FastbootInfo ("partition       descriptor    status");
  WaitForTransferComplete ();
  FastbootInfo ("");
  WaitForTransferComplete ();

  /* ---- per-partition rows ----------------------------------------------- */
  for (Idx = 0; Idx < NumKnown; Idx++) {
    CONST CHAR8          *PartName = KnownParts[Idx];
    GBL_PART_DESC_TYPE    Type;
    CONST UINT8          *Digest    = NULL;
    UINT32                DigestLen = 0;
    CONST UINT8          *PubKey    = NULL;
    UINT32                PubKeyLen = 0;
    CONST CHAR8          *DescType;
    CONST CHAR8          *RowStatus;

    /* Look up this partition's descriptor */
    Status = GblVbmetaLookupDescriptor (VbmBuf, VbmSize, PartName,
                                        &Type, &Digest, &DigestLen,
                                        &PubKey, &PubKeyLen);
    if (EFI_ERROR (Status))
      continue;  /* parse error — skip */
    if (Type == GblPartDescNone)
      continue;  /* not in vbmeta — skip silently */

    /* ---- hash descriptor ------------------------------------------------ */
    if (Type == GblPartDescHash) {
      EFI_BLOCK_IO_PROTOCOL *PartBlk  = NULL;
      EFI_HANDLE            *PartHdl  = NULL;
      CHAR16                 PartResolved[MAX_GPT_NAME_SIZE];
      UINT8                 *PartBuf  = NULL;
      UINT64                 PartSize = 0;
      UINT64                 ReadBytes;
      GBL_AvbSHA256Ctx       Ctx;
      UINT8                 *Computed;

      DescType = "hash";

      Status = LocatePartition (PartName, &PartBlk, &PartHdl,
                                        PartResolved, ARRAY_SIZE (PartResolved));
      if (EFI_ERROR (Status) || PartBlk == NULL) {
        RowStatus = "n/a";
      } else {
        PartSize  = GetPartitionSize (PartBlk);
        ReadBytes = (PartSize + PartBlk->Media->BlockSize - 1)
                    & ~(PartBlk->Media->BlockSize - 1);
        PartBuf = AllocatePool (ReadBytes);
        if (PartBuf == NULL) {
          RowStatus = "error";
        } else {
          Status = PartBlk->ReadBlocks (PartBlk, PartBlk->Media->MediaId,
                                        0, ReadBytes, PartBuf);
          if (EFI_ERROR (Status)) {
            FreePool (PartBuf);
            RowStatus = "error";
          } else {
            avb_sha256_init (&Ctx);
            avb_sha256_update (&Ctx, PartBuf, (UINT32)PartSize);
            Computed = avb_sha256_final (&Ctx);
            FreePool (PartBuf);

            if (DigestLen != GBL_VBMETA_SHA256_LEN ||
                CompareMem (Computed, Digest, GBL_VBMETA_SHA256_LEN) != 0)
              RowStatus = "mismatch";
            else
              RowStatus = "ok";
          }
        }
      }

      AsciiSPrint (Line, sizeof (Line), "%-15a %-13a %a",
                   PartName, DescType, RowStatus);
      FastbootInfo (Line);
      WaitForTransferComplete ();
      continue;
    }

    /* ---- chain descriptor ----------------------------------------------- */
    if (Type == GblPartDescChain) {
      EFI_BLOCK_IO_PROTOCOL *PartBlk  = NULL;
      EFI_HANDLE            *PartHdl  = NULL;
      CHAR16                 PartResolved[MAX_GPT_NAME_SIZE];
      UINT8                 *PartBuf  = NULL;
      UINT64                 PartSize = 0;
      UINT64                 ReadBytes;
      GBL_AVB_FOOTER         ChainFooter;

      DescType = "chain";

      Status = LocatePartition (PartName, &PartBlk, &PartHdl,
                                        PartResolved, ARRAY_SIZE (PartResolved));
      if (EFI_ERROR (Status) || PartBlk == NULL) {
        RowStatus = "n/a";
      } else {
        PartSize  = GetPartitionSize (PartBlk);
        ReadBytes = (PartSize + PartBlk->Media->BlockSize - 1)
                    & ~(PartBlk->Media->BlockSize - 1);
        PartBuf = AllocatePool (ReadBytes);
        if (PartBuf == NULL) {
          RowStatus = "error";
        } else {
          Status = PartBlk->ReadBlocks (PartBlk, PartBlk->Media->MediaId,
                                        0, ReadBytes, PartBuf);
          if (EFI_ERROR (Status)) {
            FreePool (PartBuf);
            RowStatus = "error";
          } else {
            if (EFI_ERROR (AvbParse_Footer (PartBuf, PartSize, &ChainFooter)))
              RowStatus = "unsigned";
            else
              RowStatus = "ok";
            FreePool (PartBuf);
          }
        }
      }

      AsciiSPrint (Line, sizeof (Line), "%-15a %-13a %a",
                   PartName, DescType, RowStatus);
      FastbootInfo (Line);
      WaitForTransferComplete ();
      continue;
    }

    /* ---- unsupported descriptor type (e.g. hashtree) ------------------- */
    AsciiSPrint (Line, sizeof (Line), "%-15a %-13a %a",
                 PartName, "unknown", "unknown");
    FastbootInfo (Line);
    WaitForTransferComplete ();
  }

  /* ---- blank separator before summary ----------------------------------- */
  FastbootInfo ("");
  WaitForTransferComplete ();

  /* ---- summary: vbmeta SHA-256 digest (64 hex chars, use chunking) ------ */
  {
    GBL_AvbSHA256Ctx  Ctx;
    UINT8            *Digest;
    CHAR8             DigestHex[GBL_VBMETA_HEX_LEN];

    avb_sha256_init (&Ctx);
    avb_sha256_update (&Ctx, VbmBuf, (UINT32)VbmSize);
    Digest = avb_sha256_final (&Ctx);
    GblVbmetaHexDump (Digest, GBL_VBMETA_SHA256_LEN, DigestHex, sizeof (DigestHex));

    AsciiSPrint (Line, sizeof (Line), "vbmeta digest:  %a", DigestHex);
    GblFastbootInfoLong (Line);
  }

  /* ---- summary: vbmeta flags ------------------------------------------- */
  {
    GBL_AVB_VBMETA_HEADER  Hdr;

    if (!EFI_ERROR (AvbParse_VbmetaHeader (VbmBuf, VbmSize, &Hdr))) {
      AsciiSPrint (Line, sizeof (Line), "vbmeta flags:   0x%08x",
                   (UINT32)Hdr.Flags);
      FastbootInfo (Line);
      WaitForTransferComplete ();
    }
  }

  FreePool (VbmBuf);
  FastbootOkay ("");
}

#endif /* GBL_EXPERIMENTAL_FASTBOOT_CMDS */

/* Registers all Stock commands, Publishes all stock variables
 * and partitiion sizes. base and size are the respective parameters
 * to the Fastboot Buffer used to store the downloaded image for flashing
 */
STATIC EFI_STATUS
FastbootCommandSetup (IN VOID *Base, IN UINT64 Size)
{
  EFI_STATUS Status;
  CHAR8 HWPlatformBuf[MAX_RSP_SIZE] = "\0";
  CHAR8 DeviceType[MAX_RSP_SIZE] = "\0";
  BOOLEAN BatterySocOk = FALSE;
  UINT32 BatteryVoltage = 0;
  UINT32 PartitionCount = 0;
  BOOLEAN MultiSlotBoot = PartitionHasMultiSlot ((CONST CHAR16 *)L"boot");
  MemCardType Type = UNKNOWN;
  VirtualAbMergeStatus SnapshotMergeStatus;

  mDataBuffer = Base;
  mNumDataBytes = Size;
  mFlashNumDataBytes = Size;
  mUsbDataBuffer = Base;

  mFlashDataBuffer = (CheckRootDeviceType () == NAND) ?
                           Base : (Base + MaxDownLoadSize);

  /* Find all Software Partitions in the User Partition */
  UINT32 i;
  UINT32 BlkSize = 0;
  DeviceInfo *DevInfoPtr = NULL;

  struct FastbootCmdDesc cmd_list[] = {
      /* By Default enable list is empty */
      {"", NULL},
/*CAUTION(High): Enabling these commands will allow changing the partitions
 *like system,userdata,cachec etc...
 */
#ifdef ENABLE_UPDATE_PARTITIONS_CMDS
      {"flash:", CmdFlash},
      {"erase:", CmdErase},
      {"set_active", CmdSetActive},
      {"flashing get_unlock_ability", CmdFlashingGetUnlockAbility},
      {"flashing unlock", CmdFlashingUnlock},
      {"flashing lock", CmdFlashingLock},
#endif
/*
 *CAUTION(CRITICAL): Enabling these commands will allow changes to bootimage.
 */
#ifdef ENABLE_DEVICE_CRITICAL_LOCK_UNLOCK_CMDS
      {"flashing unlock_critical", CmdFlashingUnLockCritical},
      {"flashing lock_critical", CmdFlashingLockCritical},
#endif
/*
 *CAUTION(CRITICAL): Enabling this command will allow boot with different
 *bootimage.
 */
#ifdef ENABLE_BOOT_CMD
      {"boot", CmdBoot},
#endif
      {"oem enable-charger-screen", CmdOemEnableChargerScreen},
      {"oem disable-charger-screen", CmdOemDisableChargerScreen},
      {"oem off-mode-charge", CmdOemOffModeCharger},
      {"oem select-display-panel", CmdOemSelectDisplayPanel},
      {"oem set-hw-fence-value", CmdOemSetHwFenceValue},
      {"oem set-gpu-preemption", CmdOemSetGpuPreemptionValue},
      {"oem device-info", CmdOemDevinfo},
#if HIBERNATION_SUPPORT_NO_AES
      {"oem golden-snapshot", CmdGoldenSnapshot},
#endif
      {"continue", CmdContinue},
      {"reboot", CmdReboot},
      {"reboot-bootloader", CmdRebootBootloader},
      {"getvar:", CmdGetVar},
      {"download:", CmdDownload},
      {"oem audio-framework", CmdOemAudioFrameWork},
#if defined (GBL_EXPERIMENTAL_FASTBOOT_CMDS)
      {"oem escape", CmdOemEscape},
      {"oem rpmb-lock", CmdOemRpmbLock},
      {"oem rpmb-unlock", CmdOemRpmbUnlock},
      {"oem synthesize-and-flash", CmdOemSynthesizeAndFlash},
      {"oem fix-vbmeta-footer",    CmdOemFixVbmetaFooter},
      {"oem vbmeta-status", CmdOemVbmetaStatus},
#endif
      {"oem boot-efi", CmdOemBootEfi},
      {"oem bcb-recovery", CmdOemBcbRecovery},
      {"oem bcb-fastboot", CmdOemBcbFastboot},
      {"oem bcb-clear", CmdOemBcbClear},
  };

  /* Register the commands only for non-user builds */
  Status = BoardSerialNum (StrSerialNum, sizeof (StrSerialNum));
  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Error Finding board serial num: %x\n", Status));
    return Status;
  }
  /* Publish getvar variables */
  FastbootPublishVar ("kernel", "uefi");
  AsciiSPrint (MaxDownloadSizeStr,
                  sizeof (MaxDownloadSizeStr), "%ld", MaxDownLoadSize);
  FastbootPublishVar ("max-download-size", MaxDownloadSizeStr);

  if (IsDynamicPartitionSupport ()) {
    FastbootPublishVar ("is-userspace", "no");
  }

  AsciiSPrint (FullProduct, sizeof (FullProduct), "%a", PRODUCT_NAME);
  FastbootPublishVar ("product", FullProduct);
  FastbootPublishVar ("serialno", StrSerialNum);
  FastbootPublishVar ("secure", IsSecureBootEnabled () ? "yes" : "no");

  /* gbl-chainload build info. Single source of truth: GBL_BUILD_NAME is
     defined in GblChainloadPkg.dsc and substituted in by build-inside-docker.sh
     (e.g. "mode-1-auto-debug-verbose"). Same string is in the log banner
     and FastbootMenu display, so on-device, on-screen, and host views all
     match. `boot-mode` is deliberately NOT used — reserved for the actual
     Android boot signal (bootloader/system/recovery). */
#ifndef GBL_BUILD_NAME
# define GBL_BUILD_NAME  "mode-unknown"
#endif
  FastbootPublishVar ("build-name", GBL_BUILD_NAME);
  FastbootPublishVar ("build-date", __DATE__ " " __TIME__);

  if (MultiSlotBoot) {
    /*Find ActiveSlot, bydefault _a will be the active slot
     *Populate MultiSlotMeta data will publish fastboot variables
     *like slot_successful, slot_unbootable,slot_retry_count and
     *CurrenSlot, these can modified using fastboot set_active command
     */
    FindPtnActiveSlot ();
    PopulateMultislotMetadata ();
    DEBUG ((EFI_D_VERBOSE, "Multi Slot boot is supported\n"));
  }

  GetPartitionCount (&PartitionCount);
  Status = PublishGetVarPartitionInfo (PublishedPartInfo, PartitionCount);
  if (Status != EFI_SUCCESS)
    DEBUG ((EFI_D_ERROR, "Failed to publish part info for all partitions\n"));
  BoardHwPlatformName (HWPlatformBuf, sizeof (HWPlatformBuf));
  GetRootDeviceType (DeviceType, sizeof (DeviceType));
  AsciiSPrint (StrVariant, sizeof (StrVariant), "%a %a", HWPlatformBuf,
               DeviceType);
  FastbootPublishVar ("variant", StrVariant);
  GetPageSize (&BlkSize);
  AsciiSPrint (LogicalBlkSizeStr, sizeof (LogicalBlkSizeStr), " 0x%x", BlkSize);
  FastbootPublishVar ("logical-block-size", LogicalBlkSizeStr);
  Type = CheckRootDeviceType ();
  if (Type == NAND) {
    BlkSize = NAND_PAGES_PER_BLOCK * BlkSize;
  }

  AsciiSPrint (EraseBlkSizeStr, sizeof (EraseBlkSizeStr), " 0x%x", BlkSize);
  FastbootPublishVar ("erase-block-size", EraseBlkSizeStr);
  GetDevInfo (&DevInfoPtr);
  FastbootPublishVar ("version-bootloader", DevInfoPtr->bootloader_version);
  FastbootPublishVar ("version-baseband", DevInfoPtr->radio_version);
  BatterySocOk = TargetBatterySocOk (&BatteryVoltage);
  AsciiSPrint (StrBatteryVoltage, sizeof (StrBatteryVoltage), "%d",
               BatteryVoltage);
  FastbootPublishVar ("battery-voltage", StrBatteryVoltage);
  AsciiSPrint (StrBatterySocOk, sizeof (StrBatterySocOk), "%a",
               BatterySocOk ? "yes" : "no");
  FastbootPublishVar ("battery-soc-ok", StrBatterySocOk);
  AsciiSPrint (ChargeScreenEnable, sizeof (ChargeScreenEnable), "%d",
               IsChargingScreenEnable ());
  FastbootPublishVar ("charger-screen-enabled", ChargeScreenEnable);
  AsciiSPrint (OffModeCharge, sizeof (OffModeCharge), "%d",
               IsChargingScreenEnable ());
  FastbootPublishVar ("off-mode-charge", ChargeScreenEnable);
  FastbootPublishVar ("unlocked", IsUnlocked () ? "yes" : "no");

  AsciiSPrint (StrSocVersion, sizeof (StrSocVersion), "%x",
                BoardPlatformChipVersion ());
  FastbootPublishVar ("hw-revision", StrSocVersion);

  if (IsDisableParallelDownloadFlash()) {
    FastbootPublishVar ("parallel-download-flash", "no");
  } else {
    FastbootPublishVar ("parallel-download-flash", "yes");
  }

  /* Register handlers for the supported commands*/
  UINT32 FastbootCmdCnt = sizeof (cmd_list) / sizeof (cmd_list[0]);
  for (i = 1; i < FastbootCmdCnt; i++)
    FastbootRegister (cmd_list[i].name, cmd_list[i].cb);

  if (IsDynamicPartitionSupport ()) {
    FastbootRegister ("reboot-recovery", CmdRebootRecovery);
    FastbootRegister ("reboot-fastboot", CmdRebootFastboot);
    FastbootRegister ("snapshot-update", CmdUpdateSnapshot);

    SnapshotMergeStatus = GetSnapshotMergeStatus ();

    switch (SnapshotMergeStatus) {
      case SNAPSHOTTED:
        SnapshotMergeStatus = SNAPSHOTTED;
        break;
      case MERGING:
        SnapshotMergeStatus = MERGING;
        break;
      default:
        SnapshotMergeStatus = NONE_MERGE_STATUS;
        break;
    }

    AsciiSPrint (SnapshotMergeState,
                  AsciiStrLen (VabSnapshotMergeStatus[SnapshotMergeStatus]) + 1,
                  "%a", VabSnapshotMergeStatus[SnapshotMergeStatus]);
    FastbootPublishVar ("snapshot-update-status", SnapshotMergeState);
  }

  // Read Allow Ulock Flag
  Status = ReadAllowUnlockValue (&IsAllowUnlock);
  DEBUG ((EFI_D_VERBOSE, "IsAllowUnlock is %d\n", IsAllowUnlock));

  if (Status != EFI_SUCCESS) {
    DEBUG ((EFI_D_ERROR, "Error Reading FRP partition: %r\n", Status));
    return Status;
  }

  return EFI_SUCCESS;
}

VOID *FastbootDloadBuffer (VOID)
{
  return (VOID *)mUsbDataBuffer;
}

ANDROID_FASTBOOT_STATE FastbootCurrentState (VOID)
{
  return mState;
}
