/** @file
  GpuConfig.h - Optional \EFI\GPUCHECK\gpucheck.cfg parsing.

  The file is optional by design.  If it is missing, absent, malformed or
  unreadable, GpuCheck runs on defaults and simply shows every display
  adapter it found.  Configuration must never be able to break detection.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GPUCHECK_GPUCONFIG_H_
#define GPUCHECK_GPUCONFIG_H_

#include "GpuCheck.h"

#define GPUCHECK_CONFIG_PATH  L"\\EFI\\GPUCHECK\\gpucheck.cfg"
#define GPUCHECK_CFG_MAX_SIZE 8192

typedef enum {
  ConfigEncodingOk = 0,
  ConfigEncodingUtf16Rejected     /* saved as UTF-16; cannot be parsed     */
} CONFIG_ENCODING;

typedef struct {
  BOOLEAN          Loaded;        /* a config file was found and read      */
  CONFIG_ENCODING  Encoding;      /* non-zero means the file was unusable  */

  BOOLEAN  HasExpectVendor;
  UINT16   ExpectVendor;

  BOOLEAN  HasExpectDevice;
  BOOLEAN  ExpectDeviceAny;       /* EXPECT_DEVICE=ANY                     */
  UINT16   ExpectDevice;

  BOOLEAN  AutobootWhenFound;     /* default TRUE                          */
  UINTN    AutobootDelay;         /* seconds, default 3                    */
  BOOLEAN  HaltWhenMissing;       /* default TRUE                          */
  BOOLEAN  LogEnable;             /* default TRUE (best effort)            */

  BOOLEAN  HasBootPath;
  CHAR16   BootPath[256];
} GPUCHECK_CONFIG;

VOID
GpuConfigSetDefaults (
  GPUCHECK_CONFIG  *Config
  );

/**
  Read and parse the config file from Device.  Always leaves Config in a
  usable state; returns EFI_NOT_FOUND if there was no file.
**/
EFI_STATUS
GpuConfigLoad (
  EFI_HANDLE        Device,
  GPUCHECK_CONFIG  *Config
  );

#endif /* GPUCHECK_GPUCONFIG_H_ */
