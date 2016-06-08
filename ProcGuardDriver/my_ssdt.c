#include "my_ssdt.h"
#include "message.h"

#define SEC_IMAGE 0x1000000

static CREATE_SECTION_FUNC OldNtCreateSection = NULL;

static DWORD
getSSDTIndex(PBYTE ApiCall);


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
					KdPrint(("FilePath: %ws\r\n", FilePath->Name.Buffer));
					
					// 为该事件添加一条消息
					msg = ExAllocatePoolWithTag(
						NonPagedPool, sizeof(MY_MSG_LIST_ENTRY), ALLOC_TAG);
					if (msg == NULL) {
						status = STATUS_INSUFFICIENT_RESOURCES;
						ExFreePool(FilePath);
						goto ret;
					}
					status = addMsgListEntry(msg, &FilePath->Name);

					// 激活“新消息”事件，驱动将消息传给用户
					KeSetEvent(getNewMsgEvent(), 0, TRUE);

					// 等待，直到消息被用户确认
					KeWaitForSingleObject(
						&msg->Event,
						Executive,
						KernelMode,
						0, NULL);

					confirmed = MSG_CONFIRMED(msg);

					ExFreePool(msg);
					ExFreePool(FilePath);

					// 用户拒绝
					if (!confirmed) {
						status = STATUS_ACCESS_DENIED;
						goto ret;
					}

					KdPrint((DBG_PREFIX "confirmed\r\n"));
				}
			}
		}
	}

	status = OldNtCreateSection(SectionHandle,
		DesiredAccess,
		ObjectAttributes,
		MaximumSize,
		SectionPageProtection,
		AllocationAttributes,
		FileHandle);

ret:
	return status;
}

WP_GLOBALS
disableWP_MDL(
	PDWORD Ssdt,
	DWORD NServices
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
	PMDL Mdl,
	PBYTE CallTable
	)
{
	if (Mdl != NULL) {
		MmUnmapLockedPages(CallTable, Mdl);
		IoFreeMdl(Mdl);
	}
}

PBYTE
hookSSDT(
	PBYTE ApiCall,
	PBYTE NewCall,
	PDWORD CallTable
	)
{
	PLONG target;
	DWORD indexValue;

	indexValue = getSSDTIndex(ApiCall);
	target = (PLONG)&(CallTable[indexValue]);

	return ((PBYTE)InterlockedExchange(target, (LONG)NewCall));
}

VOID
unhookSSDT(
	PBYTE ApiCall,
	PBYTE OldCall,
	PDWORD CallTable
	)
{
	hookSSDT(ApiCall, OldCall, CallTable);
}

DWORD
getSSDTIndex(
	PBYTE ApiCall
	)
{
	return *((PULONG)(ApiCall + 1));
}
