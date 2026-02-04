#pragma once

NTSTATUS EnableSelfProtection(VOID);
NTSTATUS DisableSelfProtection(VOID);
BOOLEAN IsSelfProtected(VOID);

// Protection mechanisms
NTSTATUS ProtectDriverFile(VOID);
NTSTATUS ProtectRegistryEntries(VOID);
NTSTATUS HookSystemCalls(VOID);
NTSTATUS UnhookSystemCalls(VOID);

// Monitoring
NTSTATUS MonitorRemovalAttempts(VOID);
NTSTATUS SetupProcessCreationCallback(VOID);
NTSTATUS SetupImageLoadCallback(VOID);

// Anti-debug/anti-tamper
BOOLEAN CheckForDebuggers(VOID);
BOOLEAN CheckForTampering(VOID);
VOID TriggerBSODIfTampered(VOID);