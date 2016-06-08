#pragma once

#include "process.h"

static PLIST_ENTRY ActiveProcessHead = NULL;

// ProcessListHead 和 ProcessListTail
// 对应左开右闭区间：( start, end ]
static PMY_PROC_LIST_ENTRY ProcessListHead = NULL;
static PMY_PROC_LIST_ENTRY ProcessListTail = NULL;
static PMY_PROC_LIST_ENTRY ProcessListNext = NULL;

static PMY_PROC_LIST_ENTRY FreeListHead = NULL;

//static RTL_OSVERSIONINFOEXW OsVersionInfo;


//----------------------------------------------
// STATIC FUNCTION DECLARARIONS
//----------------------------------------------
static VOID moveToFreeList(
	_In_ PMY_PROC_LIST_ENTRY Start,
	_In_ PMY_PROC_LIST_ENTRY End
	);

static PMY_PROC_LIST_ENTRY newMyProcListEntry();

static BOOLEAN initProcessList();

static NTSTATUS getImageNameFromProc(
	_In_ PEPROCESS Proc,
	_Out_writes_bytes_(BufferBytes) PVOID Buffer,
	_In_ ULONG BufferBytes,
	_Out_opt_ PULONG ReturnBytes
	);

static NTSTATUS GetProcessImageName(
	_In_ HANDLE ProcessHandle,
	_Out_ PUNICODE_STRING ProcessImageName
	);


//---------------------------------------------
// FUNCTION DEFINITIONS
//---------------------------------------------

VOID
setActiveProcessHead(
	_In_ PLIST_ENTRY Head
	)
{
	ActiveProcessHead = Head;

	//OsVersionInfo.dwOSVersionInfoSize = sizeof(RTL_OSVERSIONINFOEXW);
	//OsVersionInfo.szCSDVersion[0] = '\0';
	//RtlGetVersion(&OsVersionInfo);
}

BOOLEAN
initProcessList()
{
	if (ProcessListHead == NULL) {
		ProcessListHead = newMyProcListEntry();
		if (ProcessListHead == NULL) {
			return FALSE;
		}
	}
	else {
		// 除了链表头，都移到空闲链表
		moveToFreeList(ProcessListHead->Next, ProcessListTail);
		ProcessListHead->Next = NULL;
	}

	ProcessListTail = ProcessListHead;
	ProcessListNext = NULL;

	return TRUE;
}

NTSTATUS
getProcessList()
{
	PMY_PROC_LIST_ENTRY tempEntry;
	PLIST_ENTRY listEntry;
	NTSTATUS status = STATUS_SUCCESS;
	char *imageName;

	// 链表初始化
	if (!initProcessList()) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	if (ActiveProcessHead == NULL) {
		return STATUS_BAD_DATA;
	}

	// 设置链表
	listEntry = ActiveProcessHead->Flink;
	while (listEntry != ActiveProcessHead) {
		// 分配内存
		tempEntry = newMyProcListEntry();
		if (tempEntry == NULL) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto ret;
		}

		tempEntry->Process =
			GET_EPROCESS_FROM_LIST_ENTRY(listEntry);

		imageName = GET_IMAGE_NAME_FROM_LIST_ENTRY(listEntry);
		KdPrint((DBG_PREFIX "%s\r\n", imageName));

		ProcessListTail->Next = tempEntry;
		ProcessListTail = ProcessListTail->Next;

		listEntry = listEntry->Flink;
	}
	
ret:
	ProcessListNext = ProcessListHead->Next;
	return status;
}

PMY_PROC_LIST_ENTRY
newMyProcListEntry()
/*
	从空闲链表中分配内存或向系统申请内存
*/
{
	PMY_PROC_LIST_ENTRY res;

	if (FreeListHead != NULL) {
		res = FreeListHead;
		FreeListHead = FreeListHead->Next;
	} 
	else {
		res = (PMY_PROC_LIST_ENTRY)ExAllocatePoolWithTag(
			NonPagedPool, sizeof(MY_PROC_LIST_ENTRY), ALLOC_TAG);
	}

	if (res != NULL) {
		res->Next = NULL;
	}
	return res;
}

VOID
moveToFreeList(
	_In_ PMY_PROC_LIST_ENTRY Start,
	_In_ PMY_PROC_LIST_ENTRY End
	)
{
	if (Start == NULL ||
		End == NULL ||
		End->Next == Start
		) {
		return;
	}
	
	End->Next = FreeListHead;
	FreeListHead = Start;
}

VOID
freeListPool()
/*
	释放分配的所有内存
	应该仅在删除设备或卸载驱动的时候调用
*/
{
	PMY_PROC_LIST_ENTRY entry;

	if (ProcessListHead == NULL) {
		return;
	}

	initProcessList();

	ExFreePool(ProcessListHead);
	ProcessListHead = NULL;
	ProcessListTail = NULL;

	while (FreeListHead != NULL) {
		entry = FreeListHead;
		FreeListHead = FreeListHead->Next;
		ExFreePool(entry);
	}
}

NTSTATUS
getProcInfoNext(
	_Inout_ PMY_PROC_INFO ProcInfo
	)
{
	NTSTATUS status;
	PEPROCESS proc;

	if (ProcInfo == NULL) {
		return STATUS_INVALID_PARAMETER;
	}

	if (ProcessListHead == NULL) {
		status = getProcessList();
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	if (ProcessListNext == NULL) {
		return STATUS_END_OF_FILE;
	}

	proc = ProcessListNext->Process;

	ProcInfo->Pid = GET_PID_FROM_EPROCESS(proc);
	ProcInfo->PaPid = GET_PPID_FROM_EPROCESS(proc);

	status = getImageNameFromProc(
		proc,
		ProcInfo->ImageFileName,
		IMAGE_NAME_MAX_LEN * sizeof(WCHAR),
		&ProcInfo->nameBytes);

	ProcessListNext = ProcessListNext->Next;
	return status;
}

NTSTATUS
getImageNameFromProc(
	_In_ PEPROCESS Proc,
	_Out_writes_bytes_(BufferBytes) PVOID Buffer,
	_In_ ULONG BufferBytes,
	_Out_opt_ PULONG ReturnBytes
	)
/*
	通过 EPROCESS 获得 HANDLE
	然后调用 GetProcessImageName
*/
{
	UNICODE_STRING imageName;
	HANDLE hProcess = NULL;
	NTSTATUS status = STATUS_SUCCESS;

	imageName.Buffer = Buffer;
	imageName.MaximumLength = (USHORT)BufferBytes;

	status = ObOpenObjectByPointer(
		Proc,
		0,
		NULL,
		0,
		NULL,
		KernelMode,
		&hProcess);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = GetProcessImageName(hProcess, &imageName);
	if (status == STATUS_BUFFER_OVERFLOW) { // 简单处理  FIXME
		imageName.Buffer = L"It is too long to display.";
		imageName.Length = sizeof(L"It is too long to display.");
	}

	*ReturnBytes = imageName.Length;

	return status;
}


static BOOLEAN DbgBreak = TRUE;

VOID
printActiveProcesses()
{
	PEPROCESS proc;
	WCHAR buffer[256];
	UNICODE_STRING imageName;
	NTSTATUS status;

	HANDLE hProcess = NULL;

	PMY_PROC_LIST_ENTRY listPtr;

#if DBG
	if (DbgBreak) {
		DbgBreak = FALSE;
		__debugbreak();
	}
#endif

	KdPrint((DBG_PREFIX "printActiveProcesses()\r\n"));

	RtlInitEmptyUnicodeString(&imageName, buffer, 256 * sizeof(WCHAR));

	listPtr = ProcessListHead->Next;

	while (listPtr != NULL) {
		proc = listPtr->Process;

		status = ObOpenObjectByPointer(
			proc,
			0,
			NULL,
			0,
			NULL,
			KernelMode,
			&hProcess);

		if (NT_SUCCESS(status)) {
			status = GetProcessImageName(hProcess, &imageName);
		}

		if (!NT_SUCCESS(status)) {
			switch (status)
			{
			case STATUS_INTERNAL_ERROR:
				imageName.Buffer = L"internal error";
				break;
			case STATUS_INFO_LENGTH_MISMATCH:
				imageName.Buffer = L"INFO_LENGTH_MISMATCH";
				break;
			case STATUS_BUFFER_OVERFLOW:
				imageName.Buffer = L"BUFFER_OVERFLOW";
				break;
			case STATUS_INSUFFICIENT_RESOURCES:
				imageName.Buffer = L"INSUFFICIENT_RESOURCES";
				break;
			default:
				imageName.Buffer = L"FAILED";
				break;
			}

		}

		KdPrint((DBG_PREFIX"%lu %lu %wZ\r\n",
			GET_PID_FROM_EPROCESS(proc),
			GET_PPID_FROM_EPROCESS(proc),
			&imageName));

		listPtr = listPtr->Next;

		ZwClose(hProcess);
		hProcess = NULL;
	}
}



//------------------------------------------------------------
//	使用非硬编码的方式获取进程镜像文件名
//	来源：http://www.osronline.com/article.cfm?article=472
//	有改动
//------------------------------------------------------------

typedef NTSTATUS(*QUERY_INFO_PROCESS) (
	_In_ HANDLE ProcessHandle,
	_In_ PROCESSINFOCLASS ProcessInformationClass,
	_Out_writes_bytes_(ProcessInformationLength) PVOID ProcessInformation,
	_In_ ULONG ProcessInformationLength,
	_Out_opt_ PULONG ReturnLength
	);

QUERY_INFO_PROCESS ZwQueryInformationProcess;

NTSTATUS GetProcessImageName(
	_In_ HANDLE ProcessHandle,
	_Out_ PUNICODE_STRING ProcessImageName
	)
{
	NTSTATUS status;
	ULONG returnedLength;
	ULONG bufferLength;
	PVOID buffer;
	PUNICODE_STRING imageName;

#if DBG
	if (DbgBreak) {
		DbgBreak = FALSE;
		__debugbreak();
	}
#endif

	PAGED_CODE(); // this eliminates the possibility of the IDLE Thread/Process

	if (NULL == ZwQueryInformationProcess) {
		UNICODE_STRING routineName;

		RtlInitUnicodeString(&routineName, L"ZwQueryInformationProcess");

#pragma warning(push)
#pragma warning(disable:4055)
		ZwQueryInformationProcess =
			(QUERY_INFO_PROCESS)MmGetSystemRoutineAddress(&routineName);
#pragma warning(pop)

		if (NULL == ZwQueryInformationProcess) {
			DbgPrint("Cannot resolve ZwQueryInformationProcess\n");

			return STATUS_INTERNAL_ERROR; // 0xC00000E5L
		}
	}
	//
	// Step one - get the size we need
	//
	status = ZwQueryInformationProcess(
		ProcessHandle,
		ProcessImageFileName,
		NULL, // buffer
		0, // buffer size
		&returnedLength);

	if (STATUS_INFO_LENGTH_MISMATCH != status) {	// 0xC0000004L
		return status;
	}

	//
	// Is the passed-in buffer going to be big enough for us?  
	// This function returns a single contguous buffer model...
	//
	bufferLength = returnedLength - sizeof(UNICODE_STRING);

	if (ProcessImageName->MaximumLength < bufferLength) {
		ProcessImageName->Length = (USHORT)bufferLength;

		return STATUS_BUFFER_OVERFLOW;		// 0x80000005L
	}

	//
	// If we get here, the buffer IS going to be big enough for us, so 
	// let's allocate some storage.
	//
	buffer = ExAllocatePoolWithTag(PagedPool, returnedLength, 'ipgD');

	if (NULL == buffer) {
		return STATUS_INSUFFICIENT_RESOURCES;		// 0xC000009AL
	}

	//
	// Now lets go get the data
	//
	status = ZwQueryInformationProcess(
		ProcessHandle,
		ProcessImageFileName,
		buffer,
		returnedLength,
		&returnedLength);

	if (NT_SUCCESS(status)) {
		//
		// Ah, we got what we needed
		//
		imageName = (PUNICODE_STRING)buffer;

		RtlCopyUnicodeString(ProcessImageName, imageName);
	}

	//
	// free our buffer
	//
	ExFreePool(buffer);

	//
	// And tell the caller what happened.
	//    
	return status;

}