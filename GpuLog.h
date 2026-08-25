/** @file
  GpuLog.h - Best-effort scan log to \EFI\GPUCHECK\lastscan.txt.

  Writing is optional and never required for correct operation: some firmware
  mounts the ESP read-only, and a physically failing disk must not stop the
  on-screen diagnostic.  Every failure here is silent to the caller except
  for the returned status.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GPUCHECK_GPULOG_H_
#define GPUCHECK_GPULOG_H_

#include "GpuCheck.h"
#include "PciScan.h"

#define GPUCHECK_LOG_DIR   L"\\EFI\\GPUCHECK"
#define GPUCHECK_LOG_PATH  L"\\EFI\\GPUCHECK\\lastscan.txt"

EFI_STATUS
GpuLogWriteScan (
  EFI_HANDLE               Device,
  CONST GPU_SCAN_RESULTS  *Results
  );

#endif /* GPUCHECK_GPULOG_H_ */
