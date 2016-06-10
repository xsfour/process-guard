#include "message.h"

#include "offsets.h"

static ULONG ProcessIdOffset = 0;

static MY_MSG_LIST_ENTRY _MsgHead;
static MY_MSG_LIST_ENTRY _WaitingMsgHead;

static KSPIN_LOCK _SpinLock;
static KSPIN_LOCK _WSpinLock;

static BOOLEAN Forbidden = FALSE;

KEVENT _NewMsgEvent;

CFORCEINLINE
static VOID
initMsgHead()
{
	static BOOLEAN flag = TRUE;

	if (flag) {
		flag = FALSE;
		InitializeListHead(&_MsgHead.Links);
		InitializeListHead(&_WaitingMsgHead.Links);
	}
}

PMY_MSG_LIST_ENTRY
newMsgListEntry(
	_In_ ULONG Pid,
	_In_ PUNICODE_STRING Filename
	)
{
	KIRQL oldIrql;

	PMY_MSG_LIST_ENTRY ListEntry = ExAllocatePoolWithTag(
		NonPagedPool, sizeof(MY_MSG_LIST_ENTRY), ALLOC_TAG);
	if (ListEntry == NULL) {
		return NULL;
	}

	InitializeListHead(&ListEntry->Links);

	ListEntry->Pid = Pid;

	ListEntry->Filename.Buffer = ExAllocatePoolWithTag(
		NonPagedPool,
		Filename->Length,
		ALLOC_TAG
		);
	if (ListEntry->Filename.Buffer == NULL) {
		ExFreePool(ListEntry);
		return NULL;
	}

	ListEntry->Filename.Length = ListEntry->Filename.MaximumLength
		= Filename->Length;
	RtlCopyMemory(ListEntry->Filename.Buffer, Filename->Buffer, Filename->Length);

	ListEntry->Status = MSG_STATUS_PENDING;
	KeInitializeEvent(&ListEntry->Event, SynchronizationEvent, TRUE);

	KeAcquireSpinLock(&_SpinLock, &oldIrql);
	if (!Forbidden) {
		initMsgHead();
		AppendTailList(&_MsgHead.Links, &ListEntry->Links);
	}
	else {
		ExFreePool(ListEntry);
		ListEntry = NULL;
	}
	KeReleaseSpinLock(&_SpinLock, oldIrql);

	return ListEntry;
}

NTSTATUS
freeMsgListEntry(
	_In_ PMY_MSG_LIST_ENTRY ListEntry
	)
{
	KIRQL oldIrql;

	// 移出队列，并释放内存

	KeAcquireSpinLock(&_WSpinLock, &oldIrql);
	initMsgHead();
	RemoveEntryList(&ListEntry->Links);			// FIXME: 可能发生 FatalError
	KeReleaseSpinLock(&_WSpinLock, oldIrql);

	ExFreePool(ListEntry->Filename.Buffer);
	ExFreePool(ListEntry);

	return STATUS_SUCCESS;
}

NTSTATUS
freeAllMsgs(
	)
	// 释放所有消息，在 DriverUnload 中调用
{
	PMY_MSG_LIST_ENTRY ptr;
	PMY_MSG_LIST_ENTRY temp;

	Forbidden = TRUE;

	while (!isMsgListEmpty()) {
		queryMsgListFirst();
	}

	// 激活所有等待用户反馈的事件
	ptr = (PMY_MSG_LIST_ENTRY)_WaitingMsgHead.Links.Flink;
	while (ptr != &_WaitingMsgHead) {
		temp = (PMY_MSG_LIST_ENTRY)ptr->Links.Flink;
		ptr->Status = MSG_STATUS_ABANDONED;
		KeSetEvent(&ptr->Event, 0, FALSE);
		ptr = temp;
	}

	Forbidden = FALSE;

	return STATUS_SUCCESS;
}

PMY_MSG_LIST_ENTRY
findMsgByEvent(
	PKEVENT Event
	)
{
	return (PMY_MSG_LIST_ENTRY)((PBYTE)Event - sizeof(LIST_ENTRY));
}

PKEVENT
getNewMsgEvent()
{
	static initialized = FALSE;

	if (!initialized) {
		KeInitializeEvent(
			&_NewMsgEvent, SynchronizationEvent, TRUE);
	}

	return &_NewMsgEvent;
}

BOOLEAN
isMsgListEmpty()
{
	initMsgHead();
	return IsListEmpty(&_MsgHead.Links);
}

PMY_MSG_LIST_ENTRY
queryMsgListFirst()
{
	KIRQL oldIrql;
	PLIST_ENTRY entry;

	if (IsListEmpty(&_MsgHead.Links)) {
		return NULL;
	}

	KeAcquireSpinLock(&_SpinLock, &oldIrql);
	initMsgHead();
	entry = RemoveHeadList(&_MsgHead.Links);
	KeReleaseSpinLock(&_SpinLock, oldIrql);

	InitializeListHead(entry);

	// 移至等待用户反馈队列
	KeAcquireSpinLock(&_WSpinLock, &oldIrql);
	AppendTailList(&_WaitingMsgHead.Links, entry);
	KeReleaseSpinLock(&_WSpinLock, oldIrql);

	return (PMY_MSG_LIST_ENTRY)entry;
}

VOID
setProcessIdOffset(
	ULONG Offset
	)
{
	ProcessIdOffset = Offset;
}