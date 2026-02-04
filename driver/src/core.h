#pragma once

// Windows Headers
#include <ntddk.h>
#include <wdm.h>
#include <ntstrsafe.h>
#include <wdf.h>
#include <ntimage.h>
#ifndef MAX_PATH
#define MAX_PATH 260
#endif

// Custom headers
#include "persistent.h"
#include "protection.h"
#include "stealth.h"

// Driver Configuration
#define DRIVER_TAG 'PMON'  // Pool tag for our allocations
#define DRIVER_NAME L"ParentalMonitor"
#define DEVICE_NAME L"\\Device\\ParentalMonitor"
#define SYMLINK_NAME L"\\DosDevices\\ParentalMonitor"

// Global Driver Context
typedef struct _DRIVER_CONTEXT {
    // Core
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT DeviceObject;
    UNICODE_STRING RegistryPath;
    
    // Protection
    BOOLEAN IsProtected;
    EX_RUNDOWN_REF ProtectionRundown;
    
    // Persistence
    BOOLEAN PersistenceEstablished;
    LIST_ENTRY PersistenceList;
    KSPIN_LOCK PersistenceLock;
    
    // Configuration
    ULONG ConfigFlags;
    LARGE_INTEGER InstallTime;
    
    // Callbacks
    PVOID ProcessNotifyHandle;
    PVOID ThreadNotifyHandle;
    PVOID ImageNotifyHandle;
    
    // Communication
    PETHREAD MonitorThread;
    KEVENT MonitorEvent;
    
} DRIVER_CONTEXT, *PDRIVER_CONTEXT;

// Configuration Flags
#define CONFIG_PERSISTENT       0x00000001
#define CONFIG_STEALTH          0x00000002  
#define CONFIG_SELF_PROTECT     0x00000004
#define CONFIG_MONITOR_PROCESS  0x00000008
#define CONFIG_LOG_ACTIVITY     0x00000010

// Function declarations
NTSTATUS InitializeDriverContext(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
VOID CleanupDriverContext(VOID);
PDRIVER_CONTEXT GetDriverContext(VOID);