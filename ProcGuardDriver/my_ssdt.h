#pragma once

#include <ntifs.h>

#include "mydef.h"

#pragma pack(1)
typedef struct ServiceDescriptorEntry {
	PDWORD KiServiceTable;
	PDWORD CounterBaseTable;
	DWORD NSystemCalls;
	PDWORD KiArgumentTable;
} SDE, *PSDE;
#pragma pack()

__declspec(dllimport) SDE KeServiceDescriptorTable;


typedef struct _WP_GLOBALS {
	PUCHAR CallTable;
	PMDL Mdl;
} WP_GLOBALS, *PWP_GLOBALS;

WP_GLOBALS
disableWP_MDL(
	PDWORD Ssdt,
	DWORD NServices
	);

VOID
enableWP_MDL(
	PMDL Mdl,
	PBYTE CallTable
	);

PBYTE
hookSSDT(
	PBYTE ApiCall,
	PBYTE NewCall,
	PDWORD CallTable
	);

VOID
unhookSSDT(
	PBYTE ApiCall,
	PBYTE OldCall,
	PDWORD CallTable
	);

typedef NTSTATUS(*CREATE_SECTION_FUNC)(
	_Out_    PHANDLE            SectionHandle,
	_In_     ACCESS_MASK        DesiredAccess,
	_In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
	_In_opt_ PLARGE_INTEGER     MaximumSize,
	_In_     ULONG              SectionPageProtection,
	_In_     ULONG              AllocationAttributes,
	_In_opt_ HANDLE             FileHandle
	);
