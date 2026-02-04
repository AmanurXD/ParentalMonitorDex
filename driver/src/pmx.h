#pragma once

#include <ntddk.h>
#include <wdm.h>

#define PMX_TAG 'XMPD' // ParentalMonitorDex pool tag

#define PMX_DEVICE_NAME  L"\\Device\\ParentalMonitorDex"
#define PMX_SYMLINK_NAME L"\\DosDevices\\ParentalMonitorDex"

// IOCTLs
#define PMX_IOCTL_BASE            0x800
#define IOCTL_PMX_GET_EVENTS      CTL_CODE(FILE_DEVICE_UNKNOWN, PMX_IOCTL_BASE + 1, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_PMX_CLEAR_EVENTS    CTL_CODE(FILE_DEVICE_UNKNOWN, PMX_IOCTL_BASE + 2, METHOD_BUFFERED, FILE_WRITE_ACCESS)

typedef enum _PMX_EVENT_TYPE {
    PmxEventProcessCreate = 1,
    PmxEventProcessExit   = 2,
} PMX_EVENT_TYPE;

#define PMX_MAX_PATH_CHARS 260

typedef struct _PMX_EVENT {
    LARGE_INTEGER Timestamp;   // UTC system time
    ULONG ProcessId;
    ULONG ParentProcessId;
    PMX_EVENT_TYPE Type;
    WCHAR ImagePath[PMX_MAX_PATH_CHARS]; // best-effort, null-terminated
} PMX_EVENT, *PPMX_EVENT;

#define PMX_BUFFER_CAPACITY 1024

typedef struct _PMX_CONTEXT {
    PDEVICE_OBJECT DeviceObject;
    UNICODE_STRING SymbolicLink;

    KSPIN_LOCK BufferLock;
    PMX_EVENT Buffer[PMX_BUFFER_CAPACITY];
    ULONG Head; // next write
    ULONG Tail; // next read
    ULONG Count;

    BOOLEAN ProcessCallbackRegistered;
} PMX_CONTEXT, *PPMX_CONTEXT;

NTSTATUS PmxCreateDevice(_Inout_ PDRIVER_OBJECT DriverObject);
VOID     PmxDeleteDevice(_In_ PDRIVER_OBJECT DriverObject);

NTSTATUS PmxRegisterProcessCallback(VOID);
VOID     PmxUnregisterProcessCallback(VOID);

NTSTATUS PmxDispatchCreate(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp);
NTSTATUS PmxDispatchClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp);
NTSTATUS PmxDispatchDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp);

PPMX_CONTEXT PmxGetContext(VOID);
