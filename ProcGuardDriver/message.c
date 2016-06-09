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

NTSTATUS
initMsgListEntry(
	PMY_MSG_LIST_ENTRY ListEntry,
	PUNICODE_STRING Filename
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	KIRQL oldIrql;

	ListEntry = ExAllocatePoolWithTag(
		NonPagedPool, sizeof(MY_MSG_LIST_ENTRY), ALLOC_TAG);
	if (ListEntry == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	InitializeListHead(&ListEntry->Links);

	// 当前进程 ID
	ListEntry->Pid = GET_PID_FROM_EPROCESS(PsGetCurrentProcess());

	RtlInitUnicodeString(&ListEntry->Filename, Filename->Buffer);

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
		status = STATUS_ABANDONED;
	}
	KeReleaseSpinLock(&_SpinLock, oldIrql);

	return status;
}

NTSTATUS
freeMsgListEntry(
	PMY_MSG_LIST_ENTRY ListEntry
	)
{
	KIRQL oldIrql;

	// 移出队列，并释放内存

	KeAcquireSpinLock(&_WSpinLock, &oldIrql);
	initMsgHead();
	RemoveEntryList(&ListEntry->Links);			// FIXME: 可能发生 FatalError
	KeReleaseSpinLock(&_WSpinLock, oldIrql);

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