#include "my_ssdt.h"
#include "message.h"

#define SEC_IMAGE 0x1000000

static WP_GLOBALS WpGlobals;

static CREATE_SECTION_FUNC OldZwCreateSection = NULL;

static DWORD
getSSDTIndex(PBYTE ApiCall);


static CREATE_SECTION_FUNC
hookSSDT(
	PBYTE ApiCall,
	PBYTE NewCall,
	PDWORD CallTable
	);

static VOID
unhookSSDT(
	PBYTE ApiCall,
	PBYTE OldCall,
	PDWORD CallTable
	);

static WP_GLOBALS
disableWP_MDL(
	PDWORD Ssdt,
	DWORD NServices
	);

static VOID
enableWP_MDL(
	PMDL Mdl,
	PVOID CallTable
	);

//-----------------------------------------------------
// FUNCTION DEFINITIONS
//-----------------------------------------------------

NTSTATUS MyCreateSection(
	_Out_    PHANDLE            SectionHandle,
	_In_     ACCESS_MASK        DesiredAccess,
	_In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
	_In_opt_ PLARGE_INTEGER     MaximumSize,
	_In_     ULONG              SectionPageProtection,
	_In_     ULONG              AllocationAttributes,
	_In_opt_ HANDLE             FileHandle
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	POBJECT_NAME_INFORMATION FilePath;
	PMY_MSG_LIST_ENTRY msg = NULL;
	PFILE_OBJECT FileObject;
	BOOLEAN confirmed;
	LARGE_INTEGER timeOut =
		RtlConvertLongToLargeInteger(0);

	UNREFERENCED_PARAMETER(timeOut);

	// 对有可执行权限的加载进行拦截
	if ((AllocationAttributes == SEC_IMAGE) &&
		(SectionPageProtection & PAGE_EXECUTE)) {
		if (FileHandle) {
			status = ObReferenceObjectByHandle(
				FileHandle,
				0,
				NULL,
				KernelMode,
				(PVOID*)&FileObject,
				NULL);

			if (NT_SUCCESS(status)) {
				
				// 获取要加载的文件名
				status = IoQueryFileDosDeviceName(FileObject, &FilePath);
				ObDereferenceObject(FileObject);
				if (NT_SUCCESS(status)) {
					KdPrint((DBG_PREFIX "FilePath: %wZ\r\n", FilePath->Name));
					
					// 为该事件添加一条消息
					status = initMsgListEntry(msg, &FilePath->Name);
					if (!NT_SUCCESS(status)) {
						ExFreePool(FilePath);
						return status;
					}

					KdPrint((DBG_PREFIX "msg->Filename: %wZ\r\n", msg->Filename));

					// 激活“新消息”事件，驱动将消息传给用户
					KeSetEvent(getNewMsgEvent(), 0, TRUE);

					// 等待，直到消息被用户确认
					/*
					KeWaitForSingleObject(
						&msg->Event,
						Executive,
						KernelMode,
						0, &timeOut);
						*/
					//KdPrint((DBG_PREFIX "createSection waiting: %wZ\r\n", msg->Filename));
					//while (MSG_PENDING(msg)) {
						; // waiting
					//}
					//KdPrint((DBG_PREFIX "createSection responsed: %wZ\r\n", msg->Filename));
						confirmed = TRUE;// MSG_CONFIRMED(msg);

					freeMsgListEntry(msg);
					ExFreePool(FilePath);

					// 用户拒绝
					if (!confirmed) {
						return STATUS_ACCESS_DENIED;
					}

					KdPrint((DBG_PREFIX "confirmed\r\n"));
				}
			}
		}
	}

	status = OldZwCreateSection(SectionHandle,
		DesiredAccess,
		ObjectAttributes,
		MaximumSize,
		SectionPageProtection,
		AllocationAttributes,
		FileHandle);

	return status;
}

#pragma warning(push)
#pragma warning(disable:4054)
NTSTATUS
hook()
{
#if DBG
	// my_ssdt::hook()
	__debugbreak();
#endif

	WpGlobals = disableWP_MDL(
		KeServiceDescriptorTable.KiServiceTable,
		KeServiceDescriptorTable.NSystemCalls
		);

	if (WpGlobals.Mdl == NULL || WpGlobals.CallTable == NULL) {
		return STATUS_UNSUCCESSFUL;
	}

	OldZwCreateSection = hookSSDT(
		(PVOID)ZwCreateSection,
		(PVOID)MyCreateSection,
		WpGlobals.CallTable
		);

	enableWP_MDL(WpGlobals.Mdl, WpGlobals.CallTable);

	return STATUS_SUCCESS;
}

VOID
unhook()
{
	WpGlobals = disableWP_MDL(
		KeServiceDescriptorTable.KiServiceTable,
		KeServiceDescriptorTable.NSystemCalls
		);

	unhookSSDT(
		(PVOID)ZwCreateSection,
		(PVOID)OldZwCreateSection,
		WpGlobals.CallTable
		);

	enableWP_MDL(WpGlobals.Mdl, WpGlobals.CallTable);

	KdPrint((DBG_PREFIX "[my_ssdt::unhook]Free message list...\r\n"));
	freeAllMsgs();
}
#pragma warning(pop)

WP_GLOBALS
disableWP_MDL(
	_In_ PDWORD Ssdt,
	_In_ DWORD	NServices
	)
{
	WP_GLOBALS wpGlobals;

	KdPrint((DBG_PREFIX "ori address of SSDT:%x\r\n", Ssdt));
	KdPrint((DBG_PREFIX "nServices=%x\r\n", NServices));

	wpGlobals.Mdl = MmCreateMdl(
		NULL,
		Ssdt,
		NServices * 4);

	if (wpGlobals.Mdl == NULL) {
		KdPrint((DBG_PREFIX "MmCreateMdl failed\r\n"));
		return wpGlobals;
	}

	MmBuildMdlForNonPagedPool(wpGlobals.Mdl);

	(*(wpGlobals.Mdl)).MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;

	// maps the physical pages that are desctibed by the MDL and locks them
	wpGlobals.CallTable = (PBYTE)MmMapLockedPages(wpGlobals.Mdl, KernelMode);
	if (wpGlobals.CallTable == NULL) {
		KdPrint((DBG_PREFIX "MmMapLockedPages failed\r\n"));
		return wpGlobals;
	}

	KdPrint((DBG_PREFIX "address callTable=%x\r\n,", wpGlobals.CallTable));

	return wpGlobals;
}

VOID
enableWP_MDL(
	_In_ PMDL	Mdl,
	_In_ PVOID	CallTable
	)
{
	if (Mdl != NULL) {
		MmUnmapLockedPages(CallTable, Mdl);
		IoFreeMdl(Mdl);
	}
}

CREATE_SECTION_FUNC
hookSSDT(
	_In_ PBYTE	ApiCall,
	_In_ PBYTE	NewCall,
	_In_ PDWORD CallTable
	)
{
	PLONG target;
	DWORD indexValue;

	indexValue = getSSDTIndex(ApiCall);
	target = (PLONG)&(CallTable[indexValue]);

	return (CREATE_SECTION_FUNC)(InterlockedExchange(target, (LONG)NewCall));
}

VOID
unhookSSDT(
	_In_ PBYTE	ApiCall,
	_In_ PBYTE	OldCall,
	_In_ PDWORD CallTable
	)
{
	hookSSDT(ApiCall, OldCall, CallTable);
}

DWORD
getSSDTIndex(
	_In_ PBYTE ApiCall
	)
{
	return *((PULONG)(ApiCall + 1));
}
