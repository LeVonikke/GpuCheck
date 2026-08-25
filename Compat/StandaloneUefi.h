/** @file
  StandaloneUefi.h - Minimal, self-contained UEFI environment.

  This header exists ONLY so that GpuCheck can be compiled with a bare MSVC
  (cl.exe + link.exe /SUBSYSTEM:EFI_APPLICATION) toolchain on a machine that
  has no EDK II tree.  Every type, table and protocol below is transcribed
  from the UEFI Specification 2.10 and deliberately uses EDK II names and
  member ordering, so that the very same .c files also compile unmodified
  inside an EDK II package when GPUCHECK_EDK2 is defined.

  DO NOT reorder structure members.  Field order is ABI, not style.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef GPUCHECK_STANDALONE_UEFI_H_
#define GPUCHECK_STANDALONE_UEFI_H_

#include <stdarg.h>

/* -------------------------------------------------------------------------
 * Base types
 * ---------------------------------------------------------------------- */

typedef unsigned char        BOOLEAN;
typedef signed char          INT8;
typedef unsigned char        UINT8;
typedef short                INT16;
typedef unsigned short       UINT16;
typedef int                  INT32;
typedef unsigned int         UINT32;
typedef long long            INT64;
typedef unsigned long long   UINT64;
typedef char                 CHAR8;
typedef unsigned short       CHAR16;
typedef void                 VOID;

typedef INT64                INTN;
typedef UINT64               UINTN;

#define TRUE   ((BOOLEAN)(1 == 1))
#define FALSE  ((BOOLEAN)(0 == 1))
#ifndef NULL
#define NULL   ((VOID *)0)
#endif

#define EFIAPI __cdecl
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define STATIC static

#define MAX_UINTN  ((UINTN)0xFFFFFFFFFFFFFFFFULL)

#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))

/* -------------------------------------------------------------------------
 * GUID / status
 * ---------------------------------------------------------------------- */

typedef struct {
  UINT32  Data1;
  UINT16  Data2;
  UINT16  Data3;
  UINT8   Data4[8];
} EFI_GUID;

typedef UINTN  EFI_STATUS;
typedef VOID  *EFI_HANDLE;
typedef VOID  *EFI_EVENT;
typedef UINT64 EFI_LBA;
typedef UINTN  EFI_TPL;

#define EFI_ERROR_BIT              0x8000000000000000ULL
#define ENCODE_ERROR(a)            ((EFI_STATUS)(EFI_ERROR_BIT | (a)))
#define EFI_ERROR(a)               (((INTN)(EFI_STATUS)(a)) < 0)

#define EFI_SUCCESS                ((EFI_STATUS)0)
#define EFI_LOAD_ERROR             ENCODE_ERROR (1)
#define EFI_INVALID_PARAMETER      ENCODE_ERROR (2)
#define EFI_UNSUPPORTED            ENCODE_ERROR (3)
#define EFI_BAD_BUFFER_SIZE        ENCODE_ERROR (4)
#define EFI_BUFFER_TOO_SMALL       ENCODE_ERROR (5)
#define EFI_NOT_READY              ENCODE_ERROR (6)
#define EFI_DEVICE_ERROR           ENCODE_ERROR (7)
#define EFI_WRITE_PROTECTED        ENCODE_ERROR (8)
#define EFI_OUT_OF_RESOURCES       ENCODE_ERROR (9)
#define EFI_NOT_FOUND              ENCODE_ERROR (14)
#define EFI_ACCESS_DENIED          ENCODE_ERROR (15)
#define EFI_TIMEOUT                ENCODE_ERROR (18)
#define EFI_ABORTED                ENCODE_ERROR (21)
#define EFI_SECURITY_VIOLATION     ENCODE_ERROR (26)

/* -------------------------------------------------------------------------
 * Table header / memory / time
 * ---------------------------------------------------------------------- */

typedef struct {
  UINT64  Signature;
  UINT32  Revision;
  UINT32  HeaderSize;
  UINT32  CRC32;
  UINT32  Reserved;
} EFI_TABLE_HEADER;

typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;

typedef enum {
  AllocateAnyPages,
  AllocateMaxAddress,
  AllocateAddress,
  MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef enum {
  EfiReservedMemoryType,
  EfiLoaderCode,
  EfiLoaderData,
  EfiBootServicesCode,
  EfiBootServicesData,
  EfiRuntimeServicesCode,
  EfiRuntimeServicesData,
  EfiConventionalMemory,
  EfiUnusableMemory,
  EfiACPIReclaimMemory,
  EfiACPIMemoryNVS,
  EfiMemoryMappedIO,
  EfiMemoryMappedIOPortSpace,
  EfiPalCode,
  EfiPersistentMemory,
  EfiUnacceptedMemoryType,
  EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef struct {
  UINT32                Type;
  EFI_PHYSICAL_ADDRESS  PhysicalStart;
  EFI_VIRTUAL_ADDRESS   VirtualStart;
  UINT64                NumberOfPages;
  UINT64                Attribute;
} EFI_MEMORY_DESCRIPTOR;

typedef struct {
  UINT16  Year;        /* 1900 - 9999 */
  UINT8   Month;       /* 1 - 12      */
  UINT8   Day;         /* 1 - 31      */
  UINT8   Hour;        /* 0 - 23      */
  UINT8   Minute;      /* 0 - 59      */
  UINT8   Second;      /* 0 - 59      */
  UINT8   Pad1;
  UINT32  Nanosecond;
  INT16   TimeZone;
  UINT8   Daylight;
  UINT8   Pad2;
} EFI_TIME;

typedef struct {
  UINT32   Resolution;
  UINT32   Accuracy;
  BOOLEAN  SetsToZero;
} EFI_TIME_CAPABILITIES;

typedef enum {
  EfiResetCold,
  EfiResetWarm,
  EfiResetShutdown,
  EfiResetPlatformSpecific
} EFI_RESET_TYPE;

typedef enum {
  TimerCancel,
  TimerPeriodic,
  TimerRelative
} EFI_TIMER_DELAY;

typedef enum {
  AllHandles,
  ByRegisterNotify,
  ByProtocol
} EFI_LOCATE_SEARCH_TYPE;

typedef enum {
  EFI_NATIVE_INTERFACE
} EFI_INTERFACE_TYPE;

typedef struct {
  EFI_GUID  VendorGuid;
  VOID     *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
  EFI_HANDLE  AgentHandle;
  EFI_HANDLE  ControllerHandle;
  UINT32      Attributes;
  UINT32      OpenCount;
} EFI_OPEN_PROTOCOL_INFORMATION_ENTRY;

#define EVT_TIMER                          0x80000000
#define EVT_NOTIFY_WAIT                    0x00000100
#define EVT_NOTIFY_SIGNAL                  0x00000200

#define TPL_APPLICATION                    4
#define TPL_CALLBACK                       8
#define TPL_NOTIFY                         16

#define EFI_OPEN_PROTOCOL_GET_PROTOCOL     0x00000002

/* -------------------------------------------------------------------------
 * Simple Text Output / Input
 * ---------------------------------------------------------------------- */

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
  INT32    MaxMode;
  INT32    Mode;
  INT32    Attribute;
  INT32    CursorColumn;
  INT32    CursorRow;
  BOOLEAN  CursorVisible;
} EFI_SIMPLE_TEXT_OUTPUT_MODE;

typedef EFI_STATUS (EFIAPI *EFI_TEXT_RESET)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_TEST_STRING)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *String);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_QUERY_MODE)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber, UINTN *Columns, UINTN *Rows);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_MODE)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_ATTRIBUTE)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_CLEAR_SCREEN)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_SET_CURSOR_POSITION)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Column, UINTN Row);
typedef EFI_STATUS (EFIAPI *EFI_TEXT_ENABLE_CURSOR)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible);

struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
  EFI_TEXT_RESET                Reset;
  EFI_TEXT_STRING               OutputString;
  EFI_TEXT_TEST_STRING          TestString;
  EFI_TEXT_QUERY_MODE           QueryMode;
  EFI_TEXT_SET_MODE             SetMode;
  EFI_TEXT_SET_ATTRIBUTE        SetAttribute;
  EFI_TEXT_CLEAR_SCREEN         ClearScreen;
  EFI_TEXT_SET_CURSOR_POSITION  SetCursorPosition;
  EFI_TEXT_ENABLE_CURSOR        EnableCursor;
  EFI_SIMPLE_TEXT_OUTPUT_MODE  *Mode;
};

#define EFI_BLACK          0x00
#define EFI_BLUE           0x01
#define EFI_GREEN          0x02
#define EFI_CYAN           0x03
#define EFI_RED            0x04
#define EFI_MAGENTA        0x05
#define EFI_BROWN          0x06
#define EFI_LIGHTGRAY      0x07
#define EFI_DARKGRAY       0x08
#define EFI_LIGHTBLUE      0x09
#define EFI_LIGHTGREEN     0x0A
#define EFI_LIGHTCYAN      0x0B
#define EFI_LIGHTRED       0x0C
#define EFI_LIGHTMAGENTA   0x0D
#define EFI_YELLOW         0x0E
#define EFI_WHITE          0x0F

#define EFI_BACKGROUND_BLACK      0x00
#define EFI_BACKGROUND_BLUE       0x10
#define EFI_BACKGROUND_RED        0x40

#define EFI_TEXT_ATTR(f, b)  ((f) | ((b) << 4))

typedef struct {
  UINT16  ScanCode;
  CHAR16  UnicodeChar;
} EFI_INPUT_KEY;

#define SCAN_NULL    0x0000
#define SCAN_UP      0x0001
#define SCAN_DOWN    0x0002
#define SCAN_ESC     0x0017
#define SCAN_F1      0x000B

#define CHAR_BACKSPACE     0x0008
#define CHAR_TAB           0x0009
#define CHAR_LINEFEED      0x000A
#define CHAR_CARRIAGE_RETURN 0x000D

struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_INPUT_RESET)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN ExtendedVerification);
typedef EFI_STATUS (EFIAPI *EFI_INPUT_READ_KEY)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key);

struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
  EFI_INPUT_RESET     Reset;
  EFI_INPUT_READ_KEY  ReadKeyStroke;
  EFI_EVENT           WaitForKey;
};

/* -------------------------------------------------------------------------
 * Device Path
 * ---------------------------------------------------------------------- */

#pragma pack(1)
typedef struct {
  UINT8  Type;
  UINT8  SubType;
  UINT8  Length[2];
} EFI_DEVICE_PATH_PROTOCOL;
#pragma pack()

#define HARDWARE_DEVICE_PATH   0x01
#define ACPI_DEVICE_PATH       0x02
#define MESSAGING_DEVICE_PATH  0x03
#define MEDIA_DEVICE_PATH      0x04
#define BBS_DEVICE_PATH        0x05
#define END_DEVICE_PATH_TYPE   0x7F

#define MEDIA_FILEPATH_DP              0x04
#define END_ENTIRE_DEVICE_PATH_SUBTYPE 0xFF
#define END_INSTANCE_DEVICE_PATH_SUBTYPE 0x01

#define DevicePathType(a)       (((EFI_DEVICE_PATH_PROTOCOL *)(a))->Type)
#define DevicePathSubType(a)    (((EFI_DEVICE_PATH_PROTOCOL *)(a))->SubType)
#define DevicePathNodeLength(a) ((UINTN)(((EFI_DEVICE_PATH_PROTOCOL *)(a))->Length[0]) | \
                                 ((UINTN)(((EFI_DEVICE_PATH_PROTOCOL *)(a))->Length[1]) << 8))
#define NextDevicePathNode(a)   ((EFI_DEVICE_PATH_PROTOCOL *)((UINT8 *)(a) + DevicePathNodeLength (a)))
#define IsDevicePathEndType(a)  (DevicePathType (a) == END_DEVICE_PATH_TYPE)
#define IsDevicePathEnd(a)      (IsDevicePathEndType (a) && \
                                 DevicePathSubType (a) == END_ENTIRE_DEVICE_PATH_SUBTYPE)
#define SetDevicePathNodeLength(a, l) \
  do { \
    ((EFI_DEVICE_PATH_PROTOCOL *)(a))->Length[0] = (UINT8)((UINTN)(l) & 0xFF); \
    ((EFI_DEVICE_PATH_PROTOCOL *)(a))->Length[1] = (UINT8)(((UINTN)(l) >> 8) & 0xFF); \
  } while (0)

/* -------------------------------------------------------------------------
 * Loaded Image
 * ---------------------------------------------------------------------- */

typedef EFI_STATUS (EFIAPI *EFI_IMAGE_UNLOAD)(EFI_HANDLE ImageHandle);

typedef struct {
  UINT32                    Revision;
  EFI_HANDLE                ParentHandle;
  struct _EFI_SYSTEM_TABLE *SystemTable;
  EFI_HANDLE                DeviceHandle;
  EFI_DEVICE_PATH_PROTOCOL *FilePath;
  VOID                     *Reserved;
  UINT32                    LoadOptionsSize;
  VOID                     *LoadOptions;
  VOID                     *ImageBase;
  UINT64                    ImageSize;
  EFI_MEMORY_TYPE           ImageCodeType;
  EFI_MEMORY_TYPE           ImageDataType;
  EFI_IMAGE_UNLOAD          Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* -------------------------------------------------------------------------
 * Simple File System / File
 * ---------------------------------------------------------------------- */

struct _EFI_FILE_PROTOCOL;
typedef struct _EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

#define EFI_FILE_MODE_READ    0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE   0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE  0x8000000000000000ULL

#define EFI_FILE_READ_ONLY    0x01
#define EFI_FILE_HIDDEN       0x02
#define EFI_FILE_SYSTEM       0x04
#define EFI_FILE_DIRECTORY    0x10
#define EFI_FILE_ARCHIVE      0x20

typedef struct {
  UINT64    Size;
  UINT64    FileSize;
  UINT64    PhysicalSize;
  EFI_TIME  CreateTime;
  EFI_TIME  LastAccessTime;
  EFI_TIME  ModificationTime;
  UINT64    Attribute;
  CHAR16    FileName[1];
} EFI_FILE_INFO;

typedef EFI_STATUS (EFIAPI *EFI_FILE_OPEN)(EFI_FILE_PROTOCOL *This, EFI_FILE_PROTOCOL **NewHandle, CHAR16 *FileName, UINT64 OpenMode, UINT64 Attributes);
typedef EFI_STATUS (EFIAPI *EFI_FILE_CLOSE)(EFI_FILE_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_FILE_DELETE)(EFI_FILE_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_FILE_READ)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_WRITE)(EFI_FILE_PROTOCOL *This, UINTN *BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_POSITION)(EFI_FILE_PROTOCOL *This, UINT64 *Position);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_POSITION)(EFI_FILE_PROTOCOL *This, UINT64 Position);
typedef EFI_STATUS (EFIAPI *EFI_FILE_GET_INFO)(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN *BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_SET_INFO)(EFI_FILE_PROTOCOL *This, EFI_GUID *InformationType, UINTN BufferSize, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FILE_FLUSH)(EFI_FILE_PROTOCOL *This);

struct _EFI_FILE_PROTOCOL {
  UINT64                 Revision;
  EFI_FILE_OPEN          Open;
  EFI_FILE_CLOSE         Close;
  EFI_FILE_DELETE        Delete;
  EFI_FILE_READ          Read;
  EFI_FILE_WRITE         Write;
  EFI_FILE_GET_POSITION  GetPosition;
  EFI_FILE_SET_POSITION  SetPosition;
  EFI_FILE_GET_INFO      GetInfo;
  EFI_FILE_SET_INFO      SetInfo;
  EFI_FILE_FLUSH         Flush;
  VOID                  *OpenEx;
  VOID                  *ReadEx;
  VOID                  *WriteEx;
  VOID                  *FlushEx;
};

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef EFI_STATUS (EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
  UINT64                                       Revision;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME  OpenVolume;
};

/* -------------------------------------------------------------------------
 * PCI I/O Protocol
 * ---------------------------------------------------------------------- */

struct _EFI_PCI_IO_PROTOCOL;
typedef struct _EFI_PCI_IO_PROTOCOL EFI_PCI_IO_PROTOCOL;

typedef enum {
  EfiPciIoWidthUint8,
  EfiPciIoWidthUint16,
  EfiPciIoWidthUint32,
  EfiPciIoWidthUint64,
  EfiPciIoWidthFifoUint8,
  EfiPciIoWidthFifoUint16,
  EfiPciIoWidthFifoUint32,
  EfiPciIoWidthFifoUint64,
  EfiPciIoWidthFillUint8,
  EfiPciIoWidthFillUint16,
  EfiPciIoWidthFillUint32,
  EfiPciIoWidthFillUint64,
  EfiPciIoWidthMaximum
} EFI_PCI_IO_PROTOCOL_WIDTH;

typedef enum {
  EfiPciIoOperationBusMasterRead,
  EfiPciIoOperationBusMasterWrite,
  EfiPciIoOperationBusMasterCommonBuffer,
  EfiPciIoOperationMaximum
} EFI_PCI_IO_PROTOCOL_OPERATION;

typedef enum {
  EfiPciIoAttributeOperationGet,
  EfiPciIoAttributeOperationSet,
  EfiPciIoAttributeOperationEnable,
  EfiPciIoAttributeOperationDisable,
  EfiPciIoAttributeOperationSupported,
  EfiPciIoAttributeOperationMaximum
} EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION;

typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_POLL_IO_MEM)(EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width, UINT8 BarIndex, UINT64 Offset, UINT64 Mask, UINT64 Value, UINT64 Delay, UINT64 *Result);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_IO_MEM)(EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width, UINT8 BarIndex, UINT64 Offset, UINTN Count, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_CONFIG)(EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width, UINT32 Offset, UINTN Count, VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_COPY_MEM)(EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_WIDTH Width, UINT8 DestBarIndex, UINT64 DestOffset, UINT8 SrcBarIndex, UINT64 SrcOffset, UINTN Count);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_MAP)(EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_OPERATION Operation, VOID *HostAddress, UINTN *NumberOfBytes, EFI_PHYSICAL_ADDRESS *DeviceAddress, VOID **Mapping);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_UNMAP)(EFI_PCI_IO_PROTOCOL *This, VOID *Mapping);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER)(EFI_PCI_IO_PROTOCOL *This, EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages, VOID **HostAddress, UINT64 Attributes);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_FREE_BUFFER)(EFI_PCI_IO_PROTOCOL *This, UINTN Pages, VOID *HostAddress);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_FLUSH)(EFI_PCI_IO_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_GET_LOCATION)(EFI_PCI_IO_PROTOCOL *This, UINTN *SegmentNumber, UINTN *BusNumber, UINTN *DeviceNumber, UINTN *FunctionNumber);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_ATTRIBUTES)(EFI_PCI_IO_PROTOCOL *This, EFI_PCI_IO_PROTOCOL_ATTRIBUTE_OPERATION Operation, UINT64 Attributes, UINT64 *Result);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES)(EFI_PCI_IO_PROTOCOL *This, UINT8 BarIndex, UINT64 *Supports, VOID **Resources);
typedef EFI_STATUS (EFIAPI *EFI_PCI_IO_PROTOCOL_SET_BAR_ATTRIBUTES)(EFI_PCI_IO_PROTOCOL *This, UINT64 Attributes, UINT8 BarIndex, UINT64 *Offset, UINT64 *Length);

typedef struct {
  EFI_PCI_IO_PROTOCOL_IO_MEM  Read;
  EFI_PCI_IO_PROTOCOL_IO_MEM  Write;
} EFI_PCI_IO_PROTOCOL_ACCESS;

typedef struct {
  EFI_PCI_IO_PROTOCOL_CONFIG  Read;
  EFI_PCI_IO_PROTOCOL_CONFIG  Write;
} EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS;

struct _EFI_PCI_IO_PROTOCOL {
  EFI_PCI_IO_PROTOCOL_POLL_IO_MEM         PollMem;
  EFI_PCI_IO_PROTOCOL_POLL_IO_MEM         PollIo;
  EFI_PCI_IO_PROTOCOL_ACCESS              Mem;
  EFI_PCI_IO_PROTOCOL_ACCESS              Io;
  EFI_PCI_IO_PROTOCOL_CONFIG_ACCESS       Pci;
  EFI_PCI_IO_PROTOCOL_COPY_MEM            CopyMem;
  EFI_PCI_IO_PROTOCOL_MAP                 Map;
  EFI_PCI_IO_PROTOCOL_UNMAP               Unmap;
  EFI_PCI_IO_PROTOCOL_ALLOCATE_BUFFER     AllocateBuffer;
  EFI_PCI_IO_PROTOCOL_FREE_BUFFER         FreeBuffer;
  EFI_PCI_IO_PROTOCOL_FLUSH               Flush;
  EFI_PCI_IO_PROTOCOL_GET_LOCATION        GetLocation;
  EFI_PCI_IO_PROTOCOL_ATTRIBUTES          Attributes;
  EFI_PCI_IO_PROTOCOL_GET_BAR_ATTRIBUTES  GetBarAttributes;
  EFI_PCI_IO_PROTOCOL_SET_BAR_ATTRIBUTES  SetBarAttributes;
  UINT64                                  RomSize;
  VOID                                   *RomImage;
};

/* -------------------------------------------------------------------------
 * PCI Root Bridge I/O Protocol (used only for the deep fallback scan)
 * ---------------------------------------------------------------------- */

struct _EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL;
typedef struct _EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL;

typedef enum {
  EfiPciWidthUint8,
  EfiPciWidthUint16,
  EfiPciWidthUint32,
  EfiPciWidthUint64,
  EfiPciWidthFifoUint8,
  EfiPciWidthFifoUint16,
  EfiPciWidthFifoUint32,
  EfiPciWidthFifoUint64,
  EfiPciWidthFillUint8,
  EfiPciWidthFillUint16,
  EfiPciWidthFillUint32,
  EfiPciWidthFillUint64,
  EfiPciWidthMaximum
} EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH;

typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_IO_MEM)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH Width, UINT64 Address, UINTN Count, VOID *Buffer);

typedef struct {
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_IO_MEM  Read;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_IO_MEM  Write;
} EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ACCESS;

typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_POLL_IO_MEM)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH Width, UINT64 Address, UINT64 Mask, UINT64 Value, UINT64 Delay, UINT64 *Result);
typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_COPY_MEM)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_WIDTH Width, UINT64 DestAddress, UINT64 SrcAddress, UINTN Count);
typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_MAP)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, UINTN Operation, VOID *HostAddress, UINTN *NumberOfBytes, EFI_PHYSICAL_ADDRESS *DeviceAddress, VOID **Mapping);
typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_UNMAP)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, VOID *Mapping);
typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ALLOCATE_BUFFER)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages, VOID **HostAddress, UINT64 Attributes);
typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_FREE_BUFFER)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, UINTN Pages, VOID *HostAddress);
typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_FLUSH)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This);
typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GET_ATTRIBUTES)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, UINT64 *Supports, UINT64 *Attributes);
typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_SET_ATTRIBUTES)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, UINT64 Attributes, UINT64 *ResourceBase, UINT64 *ResourceLength);
typedef EFI_STATUS (EFIAPI *EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_CONFIGURATION)(EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL *This, VOID **Resources);

struct _EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL {
  EFI_HANDLE                                       ParentHandle;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_POLL_IO_MEM      PollMem;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_POLL_IO_MEM      PollIo;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ACCESS           Mem;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ACCESS           Io;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ACCESS           Pci;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_COPY_MEM         CopyMem;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_MAP              Map;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_UNMAP            Unmap;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_ALLOCATE_BUFFER  AllocateBuffer;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_FREE_BUFFER      FreeBuffer;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_FLUSH            Flush;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_GET_ATTRIBUTES   GetAttributes;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_SET_ATTRIBUTES   SetAttributes;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL_CONFIGURATION    Configuration;
  UINT32                                           SegmentNumber;
};

/*
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL config-space address encoding:
    bits  7..0  Register (low 8 bits)
    bits 15..8  Function
    bits 23..16 Device
    bits 31..24 Bus
    bits 63..32 ExtendedRegister
*/
#define EFI_PCI_ADDRESS(bus, dev, func, reg)                 \
  ((UINT64)((((UINT64)(UINT8)(bus))  << 24) |                \
            (((UINT64)(UINT8)(dev))  << 16) |                \
            (((UINT64)(UINT8)(func)) <<  8) |                \
            (((UINT64)(UINT32)(reg)) < 256                   \
               ? ((UINT64)(UINT32)(reg))                     \
               : (((UINT64)(UINT32)(reg)) << 32))))

/* -------------------------------------------------------------------------
 * Boot Services
 * ---------------------------------------------------------------------- */

struct _EFI_BOOT_SERVICES;
typedef struct _EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
struct _EFI_RUNTIME_SERVICES;
typedef struct _EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;
struct _EFI_SYSTEM_TABLE;
typedef struct _EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

typedef VOID (EFIAPI *EFI_EVENT_NOTIFY)(EFI_EVENT Event, VOID *Context);

typedef EFI_TPL    (EFIAPI *EFI_RAISE_TPL)(EFI_TPL NewTpl);
typedef VOID       (EFIAPI *EFI_RESTORE_TPL)(EFI_TPL OldTpl);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_PAGES)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages, EFI_PHYSICAL_ADDRESS *Memory);
typedef EFI_STATUS (EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);
typedef EFI_STATUS (EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap, UINTN *MapKey, UINTN *DescriptorSize, UINT32 *DescriptorVersion);
typedef EFI_STATUS (EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE PoolType, UINTN Size, VOID **Buffer);
typedef EFI_STATUS (EFIAPI *EFI_FREE_POOL)(VOID *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_CREATE_EVENT)(UINT32 Type, EFI_TPL NotifyTpl, EFI_EVENT_NOTIFY NotifyFunction, VOID *NotifyContext, EFI_EVENT *Event);
typedef EFI_STATUS (EFIAPI *EFI_SET_TIMER)(EFI_EVENT Event, EFI_TIMER_DELAY Type, UINT64 TriggerTime);
typedef EFI_STATUS (EFIAPI *EFI_WAIT_FOR_EVENT)(UINTN NumberOfEvents, EFI_EVENT *Event, UINTN *Index);
typedef EFI_STATUS (EFIAPI *EFI_SIGNAL_EVENT)(EFI_EVENT Event);
typedef EFI_STATUS (EFIAPI *EFI_CLOSE_EVENT)(EFI_EVENT Event);
typedef EFI_STATUS (EFIAPI *EFI_CHECK_EVENT)(EFI_EVENT Event);
typedef EFI_STATUS (EFIAPI *EFI_INSTALL_PROTOCOL_INTERFACE)(EFI_HANDLE *Handle, EFI_GUID *Protocol, EFI_INTERFACE_TYPE InterfaceType, VOID *Interface);
typedef EFI_STATUS (EFIAPI *EFI_REINSTALL_PROTOCOL_INTERFACE)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID *OldInterface, VOID *NewInterface);
typedef EFI_STATUS (EFIAPI *EFI_UNINSTALL_PROTOCOL_INTERFACE)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID *Interface);
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface);
typedef EFI_STATUS (EFIAPI *EFI_REGISTER_PROTOCOL_NOTIFY)(EFI_GUID *Protocol, EFI_EVENT Event, VOID **Registration);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE)(EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol, VOID *SearchKey, UINTN *BufferSize, EFI_HANDLE *Buffer);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_DEVICE_PATH)(EFI_GUID *Protocol, EFI_DEVICE_PATH_PROTOCOL **DevicePath, EFI_HANDLE *Device);
typedef EFI_STATUS (EFIAPI *EFI_INSTALL_CONFIGURATION_TABLE)(EFI_GUID *Guid, VOID *Table);
typedef EFI_STATUS (EFIAPI *EFI_IMAGE_LOAD)(BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle, EFI_DEVICE_PATH_PROTOCOL *DevicePath, VOID *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle);
typedef EFI_STATUS (EFIAPI *EFI_IMAGE_START)(EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData);
typedef EFI_STATUS (EFIAPI *EFI_EXIT)(EFI_HANDLE ImageHandle, EFI_STATUS ExitStatus, UINTN ExitDataSize, CHAR16 *ExitData);
typedef EFI_STATUS (EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE ImageHandle, UINTN MapKey);
typedef EFI_STATUS (EFIAPI *EFI_GET_NEXT_MONOTONIC_COUNT)(UINT64 *Count);
typedef EFI_STATUS (EFIAPI *EFI_STALL)(UINTN Microseconds);
typedef EFI_STATUS (EFIAPI *EFI_SET_WATCHDOG_TIMER)(UINTN Timeout, UINT64 WatchdogCode, UINTN DataSize, CHAR16 *WatchdogData);
typedef EFI_STATUS (EFIAPI *EFI_CONNECT_CONTROLLER)(EFI_HANDLE ControllerHandle, EFI_HANDLE *DriverImageHandle, EFI_DEVICE_PATH_PROTOCOL *RemainingDevicePath, BOOLEAN Recursive);
typedef EFI_STATUS (EFIAPI *EFI_DISCONNECT_CONTROLLER)(EFI_HANDLE ControllerHandle, EFI_HANDLE DriverImageHandle, EFI_HANDLE ChildHandle);
typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL)(EFI_HANDLE Handle, EFI_GUID *Protocol, VOID **Interface, EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, UINT32 Attributes);
typedef EFI_STATUS (EFIAPI *EFI_CLOSE_PROTOCOL)(EFI_HANDLE Handle, EFI_GUID *Protocol, EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle);
typedef EFI_STATUS (EFIAPI *EFI_OPEN_PROTOCOL_INFORMATION)(EFI_HANDLE Handle, EFI_GUID *Protocol, EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **EntryBuffer, UINTN *EntryCount);
typedef EFI_STATUS (EFIAPI *EFI_PROTOCOLS_PER_HANDLE)(EFI_HANDLE Handle, EFI_GUID ***ProtocolBuffer, UINTN *ProtocolBufferCount);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE_BUFFER)(EFI_LOCATE_SEARCH_TYPE SearchType, EFI_GUID *Protocol, VOID *SearchKey, UINTN *NoHandles, EFI_HANDLE **Buffer);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *Protocol, VOID *Registration, VOID **Interface);
typedef EFI_STATUS (EFIAPI *EFI_INSTALL_MULTIPLE_PROTOCOL_INTERFACES)(EFI_HANDLE *Handle, ...);
typedef EFI_STATUS (EFIAPI *EFI_UNINSTALL_MULTIPLE_PROTOCOL_INTERFACES)(EFI_HANDLE Handle, ...);
typedef EFI_STATUS (EFIAPI *EFI_CALCULATE_CRC32)(VOID *Data, UINTN DataSize, UINT32 *Crc32);
typedef VOID       (EFIAPI *EFI_COPY_MEM)(VOID *Destination, VOID *Source, UINTN Length);
typedef VOID       (EFIAPI *EFI_SET_MEM)(VOID *Buffer, UINTN Size, UINT8 Value);
typedef EFI_STATUS (EFIAPI *EFI_CREATE_EVENT_EX)(UINT32 Type, EFI_TPL NotifyTpl, EFI_EVENT_NOTIFY NotifyFunction, CONST VOID *NotifyContext, CONST EFI_GUID *EventGroup, EFI_EVENT *Event);

struct _EFI_BOOT_SERVICES {
  EFI_TABLE_HEADER                            Hdr;
  EFI_RAISE_TPL                               RaiseTPL;
  EFI_RESTORE_TPL                             RestoreTPL;
  EFI_ALLOCATE_PAGES                          AllocatePages;
  EFI_FREE_PAGES                              FreePages;
  EFI_GET_MEMORY_MAP                          GetMemoryMap;
  EFI_ALLOCATE_POOL                           AllocatePool;
  EFI_FREE_POOL                               FreePool;
  EFI_CREATE_EVENT                            CreateEvent;
  EFI_SET_TIMER                               SetTimer;
  EFI_WAIT_FOR_EVENT                          WaitForEvent;
  EFI_SIGNAL_EVENT                            SignalEvent;
  EFI_CLOSE_EVENT                             CloseEvent;
  EFI_CHECK_EVENT                             CheckEvent;
  EFI_INSTALL_PROTOCOL_INTERFACE              InstallProtocolInterface;
  EFI_REINSTALL_PROTOCOL_INTERFACE            ReinstallProtocolInterface;
  EFI_UNINSTALL_PROTOCOL_INTERFACE            UninstallProtocolInterface;
  EFI_HANDLE_PROTOCOL                         HandleProtocol;
  VOID                                       *Reserved;
  EFI_REGISTER_PROTOCOL_NOTIFY                RegisterProtocolNotify;
  EFI_LOCATE_HANDLE                           LocateHandle;
  EFI_LOCATE_DEVICE_PATH                      LocateDevicePath;
  EFI_INSTALL_CONFIGURATION_TABLE             InstallConfigurationTable;
  EFI_IMAGE_LOAD                              LoadImage;
  EFI_IMAGE_START                             StartImage;
  EFI_EXIT                                    Exit;
  EFI_IMAGE_UNLOAD                            UnloadImage;
  EFI_EXIT_BOOT_SERVICES                      ExitBootServices;
  EFI_GET_NEXT_MONOTONIC_COUNT                GetNextMonotonicCount;
  EFI_STALL                                   Stall;
  EFI_SET_WATCHDOG_TIMER                      SetWatchdogTimer;
  EFI_CONNECT_CONTROLLER                      ConnectController;
  EFI_DISCONNECT_CONTROLLER                   DisconnectController;
  EFI_OPEN_PROTOCOL                           OpenProtocol;
  EFI_CLOSE_PROTOCOL                          CloseProtocol;
  EFI_OPEN_PROTOCOL_INFORMATION               OpenProtocolInformation;
  EFI_PROTOCOLS_PER_HANDLE                    ProtocolsPerHandle;
  EFI_LOCATE_HANDLE_BUFFER                    LocateHandleBuffer;
  EFI_LOCATE_PROTOCOL                         LocateProtocol;
  EFI_INSTALL_MULTIPLE_PROTOCOL_INTERFACES    InstallMultipleProtocolInterfaces;
  EFI_UNINSTALL_MULTIPLE_PROTOCOL_INTERFACES  UninstallMultipleProtocolInterfaces;
  EFI_CALCULATE_CRC32                         CalculateCrc32;
  EFI_COPY_MEM                                CopyMem;
  EFI_SET_MEM                                 SetMem;
  EFI_CREATE_EVENT_EX                         CreateEventEx;
};

/* -------------------------------------------------------------------------
 * Runtime Services
 * ---------------------------------------------------------------------- */

typedef EFI_STATUS (EFIAPI *EFI_GET_TIME)(EFI_TIME *Time, EFI_TIME_CAPABILITIES *Capabilities);
typedef EFI_STATUS (EFIAPI *EFI_SET_TIME)(EFI_TIME *Time);
typedef EFI_STATUS (EFIAPI *EFI_GET_WAKEUP_TIME)(BOOLEAN *Enabled, BOOLEAN *Pending, EFI_TIME *Time);
typedef EFI_STATUS (EFIAPI *EFI_SET_WAKEUP_TIME)(BOOLEAN Enable, EFI_TIME *Time);
typedef EFI_STATUS (EFIAPI *EFI_SET_VIRTUAL_ADDRESS_MAP)(UINTN MemoryMapSize, UINTN DescriptorSize, UINT32 DescriptorVersion, EFI_MEMORY_DESCRIPTOR *VirtualMap);
typedef EFI_STATUS (EFIAPI *EFI_CONVERT_POINTER)(UINTN DebugDisposition, VOID **Address);
typedef EFI_STATUS (EFIAPI *EFI_GET_VARIABLE)(CHAR16 *VariableName, EFI_GUID *VendorGuid, UINT32 *Attributes, UINTN *DataSize, VOID *Data);
typedef EFI_STATUS (EFIAPI *EFI_GET_NEXT_VARIABLE_NAME)(UINTN *VariableNameSize, CHAR16 *VariableName, EFI_GUID *VendorGuid);
typedef EFI_STATUS (EFIAPI *EFI_SET_VARIABLE)(CHAR16 *VariableName, EFI_GUID *VendorGuid, UINT32 Attributes, UINTN DataSize, VOID *Data);
typedef EFI_STATUS (EFIAPI *EFI_GET_NEXT_HIGH_MONO_COUNT)(UINT32 *HighCount);
typedef VOID       (EFIAPI *EFI_RESET_SYSTEM)(EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, UINTN DataSize, VOID *ResetData);
typedef EFI_STATUS (EFIAPI *EFI_UPDATE_CAPSULE)(VOID **CapsuleHeaderArray, UINTN CapsuleCount, EFI_PHYSICAL_ADDRESS ScatterGatherList);
typedef EFI_STATUS (EFIAPI *EFI_QUERY_CAPSULE_CAPABILITIES)(VOID **CapsuleHeaderArray, UINTN CapsuleCount, UINT64 *MaximumCapsuleSize, EFI_RESET_TYPE *ResetType);
typedef EFI_STATUS (EFIAPI *EFI_QUERY_VARIABLE_INFO)(UINT32 Attributes, UINT64 *MaximumVariableStorageSize, UINT64 *RemainingVariableStorageSize, UINT64 *MaximumVariableSize);

struct _EFI_RUNTIME_SERVICES {
  EFI_TABLE_HEADER                Hdr;
  EFI_GET_TIME                    GetTime;
  EFI_SET_TIME                    SetTime;
  EFI_GET_WAKEUP_TIME             GetWakeupTime;
  EFI_SET_WAKEUP_TIME             SetWakeupTime;
  EFI_SET_VIRTUAL_ADDRESS_MAP     SetVirtualAddressMap;
  EFI_CONVERT_POINTER             ConvertPointer;
  EFI_GET_VARIABLE                GetVariable;
  EFI_GET_NEXT_VARIABLE_NAME      GetNextVariableName;
  EFI_SET_VARIABLE                SetVariable;
  EFI_GET_NEXT_HIGH_MONO_COUNT    GetNextHighMonotonicCount;
  EFI_RESET_SYSTEM                ResetSystem;
  EFI_UPDATE_CAPSULE              UpdateCapsule;
  EFI_QUERY_CAPSULE_CAPABILITIES  QueryCapsuleCapabilities;
  EFI_QUERY_VARIABLE_INFO         QueryVariableInfo;
};

/* -------------------------------------------------------------------------
 * System Table
 * ---------------------------------------------------------------------- */

struct _EFI_SYSTEM_TABLE {
  EFI_TABLE_HEADER                  Hdr;
  CHAR16                           *FirmwareVendor;
  UINT32                            FirmwareRevision;
  EFI_HANDLE                        ConsoleInHandle;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL   *ConIn;
  EFI_HANDLE                        ConsoleOutHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *ConOut;
  EFI_HANDLE                        StandardErrorHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *StdErr;
  EFI_RUNTIME_SERVICES             *RuntimeServices;
  EFI_BOOT_SERVICES                *BootServices;
  UINTN                             NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE          *ConfigurationTable;
};

/* -------------------------------------------------------------------------
 * Protocol GUIDs
 * ---------------------------------------------------------------------- */

extern EFI_GUID  gEfiPciIoProtocolGuid;
extern EFI_GUID  gEfiPciRootBridgeIoProtocolGuid;
extern EFI_GUID  gEfiLoadedImageProtocolGuid;
extern EFI_GUID  gEfiDevicePathProtocolGuid;
extern EFI_GUID  gEfiSimpleFileSystemProtocolGuid;
extern EFI_GUID  gEfiFileInfoGuid;
extern EFI_GUID  gEfiGlobalVariableGuid;

/* -------------------------------------------------------------------------
 * Globals + library shims (names deliberately match EDK II)
 * ---------------------------------------------------------------------- */

extern EFI_SYSTEM_TABLE     *gST;
extern EFI_BOOT_SERVICES    *gBS;
extern EFI_RUNTIME_SERVICES *gRT;
extern EFI_HANDLE            gImageHandle;

VOID   StandaloneUefiInit (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

UINTN  Print (CONST CHAR16 *Format, ...);
UINTN  VPrint (CONST CHAR16 *Format, va_list Marker);

VOID  *AllocateZeroPool (UINTN Size);
VOID  *AllocatePool (UINTN Size);
VOID   FreePool (VOID *Buffer);
VOID   CopyMem (VOID *Dest, CONST VOID *Src, UINTN Length);
VOID   ZeroMem (VOID *Buffer, UINTN Length);
VOID   SetMem (VOID *Buffer, UINTN Length, UINT8 Value);
INTN   CompareMem (CONST VOID *A, CONST VOID *B, UINTN Length);

UINTN  StrLen (CONST CHAR16 *S);
UINTN  AsciiStrLen (CONST CHAR8 *S);
INTN   StrCmp (CONST CHAR16 *A, CONST CHAR16 *B);
VOID   StrCpy16 (CHAR16 *Dest, UINTN DestMax, CONST CHAR16 *Src);
VOID   StrCat16 (CHAR16 *Dest, UINTN DestMax, CONST CHAR16 *Src);

EFI_DEVICE_PATH_PROTOCOL *DevicePathFromHandle (EFI_HANDLE Handle);
EFI_DEVICE_PATH_PROTOCOL *FileDevicePath (EFI_HANDLE Device, CONST CHAR16 *FileName);

#endif /* GPUCHECK_STANDALONE_UEFI_H_ */
