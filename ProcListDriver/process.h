#pragma once

#include <ntifs.h>

#include "mydef.h"
#include "offsets.h"

//---------------------------------------
// DEFINE
//---------------------------------------


//-----------------------------------------------
// GLOBAL PARAMETERS
//-----------------------------------------------


//-----------------------------------------------
// TYPES
//-----------------------------------------------
typedef struct _MY_PROC_INFO {
	ULONG Pid;
	ULONG PaPid;
	ULONG nameBytes;
	WCHAR ImageFileName[IMAGE_NAME_MAX_LEN];
}MY_PROC_INFO, *PMY_PROC_INFO;

typedef struct _MY_PROC_LIST_ENTRY {
	struct _MY_PROC_LIST_ENTRY *Next;
	PEPROCESS Process;
}MY_PROC_LIST_ENTRY, *PMY_PROC_LIST_ENTRY;


//-----------------------------------------------
// FUNCTIONS DECLARATION
//-----------------------------------------------

NTSTATUS getProcessList();

NTSTATUS getProcInfoNext(
	_Inout_ PMY_PROC_INFO ProcInfo
	);

NTSTATUS hideProcess(
	_In_ PCHAR name
	);

VOID freeListPool();

VOID setActiveProcessHead(
	_In_ PLIST_ENTRY Head
	);

VOID printActiveProcesses();
