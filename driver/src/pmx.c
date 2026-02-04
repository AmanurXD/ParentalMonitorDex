#include "pmx.h"

static PMX_CONTEXT g_PmxContext;
static UNICODE_STRING g_DeviceName = RTL_CONSTANT_STRING(PMX_DEVICE_NAME);
static UNICODE_STRING g_SymbolicLink = RTL_CONSTANT_STRING(PMX_SYMLINK_NAME);

static VOID PmxPushEvent(_In_ PMX_EVENT_TYPE Type, _In_ ULONG Pid, _In_ ULONG ParentPid, _In_ PCUNICODE_STRING ImagePath)
{
    KIRQL oldIrql;
    KeQuerySystemTime(&g_PmxContext.Buffer[g_PmxContext.Head].Timestamp);

    g_PmxContext.Buffer[g_PmxContext.Head].Type = Type;
    g_PmxContext.Buffer[g_PmxContext.Head].ProcessId = Pid;
    g_PmxContext.Buffer[g_PmxContext.Head].ParentProcessId = ParentPid;

    RtlZeroMemory(g_PmxContext.Buffer[g_PmxContext.Head].ImagePath, sizeof(g_PmxContext.Buffer[g_PmxContext.Head].ImagePath));
    if (ImagePath && ImagePath->Buffer && ImagePath->Length > 0) {
        USHORT copyLen = min(ImagePath->Length, (PMX_MAX_PATH_CHARS - 1) * sizeof(WCHAR));
        RtlCopyMemory(g_PmxContext.Buffer[g_PmxContext.Head].ImagePath, ImagePath->Buffer, copyLen);
        g_PmxContext.Buffer[g_PmxContext.Head].ImagePath[copyLen / sizeof(WCHAR)] = L'\0';
    }

    KeAcquireSpinLock(&g_PmxContext.BufferLock, &oldIrql);
    g_PmxContext.Head = (g_PmxContext.Head + 1) % PMX_BUFFER_CAPACITY;
    if (g_PmxContext.Count == PMX_BUFFER_CAPACITY) {
        // overwrite oldest
        g_PmxContext.Tail = (g_PmxContext.Tail + 1) % PMX_BUFFER_CAPACITY;
    } else {
        g_PmxContext.Count++;
    }
    KeReleaseSpinLock(&g_PmxContext.BufferLock, oldIrql);
}

static VOID PmxProcessNotify(_Inout_ PEPROCESS Process, _In_ HANDLE ProcessId, _Inout_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo)
{
    UNREFERENCED_PARAMETER(Process);

    if (CreateInfo) {
        // Process creation
        PCUNICODE_STRING img = CreateInfo->ImageFileName;
        ULONG pid = HandleToULong(ProcessId);
        ULONG ppid = CreateInfo->ParentProcessId ? HandleToULong(CreateInfo->ParentProcessId) : 0;
        PmxPushEvent(PmxEventProcessCreate, pid, ppid, img);
    } else {
        // Process exit
        ULONG pid = HandleToULong(ProcessId);
        PmxPushEvent(PmxEventProcessExit, pid, 0, NULL);
    }
}

PPMX_CONTEXT PmxGetContext(VOID)
{
    return &g_PmxContext;
}

NTSTATUS PmxCreateDevice(_Inout_ PDRIVER_OBJECT DriverObject)
{
    PDEVICE_OBJECT deviceObject = NULL;
    NTSTATUS status = IoCreateDevice(
        DriverObject,
        0,
        &g_DeviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject);

    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymbolicLink, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    deviceObject->Flags |= DO_DIRECT_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    g_PmxContext.DeviceObject = deviceObject;
    RtlInitUnicodeString(&g_PmxContext.SymbolicLink, PMX_SYMLINK_NAME);
    return STATUS_SUCCESS;
}

VOID PmxDeleteDevice(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    if (g_PmxContext.SymbolicLink.Buffer) {
        IoDeleteSymbolicLink(&g_SymbolicLink);
    }
    if (g_PmxContext.DeviceObject) {
        IoDeleteDevice(g_PmxContext.DeviceObject);
        g_PmxContext.DeviceObject = NULL;
    }
}

NTSTATUS PmxRegisterProcessCallback(VOID)
{
    NTSTATUS status = PsSetCreateProcessNotifyRoutineEx(PmxProcessNotify, FALSE);
    if (NT_SUCCESS(status)) {
        g_PmxContext.ProcessCallbackRegistered = TRUE;
    }
    return status;
}

VOID PmxUnregisterProcessCallback(VOID)
{
    if (g_PmxContext.ProcessCallbackRegistered) {
        PsSetCreateProcessNotifyRoutineEx(PmxProcessNotify, TRUE);
        g_PmxContext.ProcessCallbackRegistered = FALSE;
    }
}

static VOID PmxCompleteIrp(_Inout_ PIRP Irp, _In_ NTSTATUS Status, _In_ ULONG_PTR Info)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Info;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

NTSTATUS PmxDispatchCreate(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    PmxCompleteIrp(Irp, STATUS_SUCCESS, 0);
    return STATUS_SUCCESS;
}

NTSTATUS PmxDispatchClose(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    PmxCompleteIrp(Irp, STATUS_SUCCESS, 0);
    return STATUS_SUCCESS;
}

static ULONG PmxCopyEventsToBuffer(_Out_writes_bytes_(OutBufferSize) PVOID OutBuffer, _In_ ULONG OutBufferSize)
{
    ULONG copied = 0;
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_PmxContext.BufferLock, &oldIrql);

    while (g_PmxContext.Count > 0 && (copied + 1) * sizeof(PMX_EVENT) <= OutBufferSize) {
        PPMX_EVENT dest = (PPMX_EVENT)((PUCHAR)OutBuffer + copied * sizeof(PMX_EVENT));
        *dest = g_PmxContext.Buffer[g_PmxContext.Tail];
        g_PmxContext.Tail = (g_PmxContext.Tail + 1) % PMX_BUFFER_CAPACITY;
        g_PmxContext.Count--;
        copied++;
    }

    KeReleaseSpinLock(&g_PmxContext.BufferLock, oldIrql);
    return copied;
}

static VOID PmxClearEvents(VOID)
{
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_PmxContext.BufferLock, &oldIrql);
    g_PmxContext.Head = g_PmxContext.Tail = g_PmxContext.Count = 0;
    KeReleaseSpinLock(&g_PmxContext.BufferLock, oldIrql);
}

NTSTATUS PmxDispatchDeviceControl(_In_ PDEVICE_OBJECT DeviceObject, _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR info = 0;

    switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
    case IOCTL_PMX_GET_EVENTS:
        if (irpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(PMX_EVENT)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }
        {
            ULONG maxBytes = irpSp->Parameters.DeviceIoControl.OutputBufferLength;
            ULONG copied = PmxCopyEventsToBuffer(Irp->AssociatedIrp.SystemBuffer, maxBytes);
            info = copied * sizeof(PMX_EVENT);
            status = STATUS_SUCCESS;
        }
        break;

    case IOCTL_PMX_CLEAR_EVENTS:
        PmxClearEvents();
        status = STATUS_SUCCESS;
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    PmxCompleteIrp(Irp, status, info);
    return status;
}

static VOID PmxInitContext(VOID)
{
    RtlZeroMemory(&g_PmxContext, sizeof(g_PmxContext));
    KeInitializeSpinLock(&g_PmxContext.BufferLock);
    g_PmxContext.Head = g_PmxContext.Tail = g_PmxContext.Count = 0;
    g_PmxContext.ProcessCallbackRegistered = FALSE;
}

static VOID PmxSetDispatch(_Inout_ PDRIVER_OBJECT DriverObject)
{
    for (UINT32 i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = PmxDispatchCreate;
    }
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = PmxDispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PmxDispatchDeviceControl;
}

VOID PmxUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    PmxUnregisterProcessCallback();
    PmxDeleteDevice(DriverObject);
}

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;

    PmxInitContext();
    PmxSetDispatch(DriverObject);

    status = PmxCreateDevice(DriverObject);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = PmxRegisterProcessCallback();
    if (!NT_SUCCESS(status)) {
        PmxDeleteDevice(DriverObject);
        return status;
    }

    DriverObject->DriverUnload = PmxUnload;
    return STATUS_SUCCESS;
}
