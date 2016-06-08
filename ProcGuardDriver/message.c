#include "message.h"

static ULONG ProcessIdOffset = 0;

static MY_MSG_LIST_ENTRY _MsgHead;

static KSPIN_LOCK _SpinLock;

KEVENT _NewMsgEvent;

#define GET_PID_FROM_EPROCESS(_Eprocess) \
	(*(PULONG)((PBYTE)(_Eprocess) + (ProcessIdOffset)))

CFORCEINLINE
static VOID
initMsgHead()
{
	static BOOLEAN flag = TRUE;

	if (flag) {
		flag = FALSE;
		InitializeListHead(&_MsgHead.Links);
	}
}

NTSTATUS
addMsgListEntry(
	PMY_MSG_LIST_ENTRY ListEntry,
	PUNICODE_STRING Filename
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	KIRQL oldIrql;

	if (ListEntry == NULL) {
		return STATUS_INVALID_PARAMETER;
	}

	ListEntry->Pid = GET_PID_FROM_EPROCESS(PsGetCurrentProcess());

	RtlInitUnicodeString(&ListEntry->Filename, Filename->Buffer);

	ListEntry->Status = MSG_STATUS_PENDING;
	KeInitializeEvent(&ListEntry->Event, SynchronizationEvent, TRUE);

	KeAcquireSpinLock(&_SpinLock, &oldIrql);
	initMsgHead();
	AppendTailList(&_MsgHead.Links, &ListEntry->Links);
	KeReleaseSpinLock(&_SpinLock, oldIrql);

	return status;
}

PMY_MSG_LIST_ENTRY
getMsgListFirst()
{
	if (IsListEmpty(&_MsgHead.Links)) {
		return NULL;
	}

	return (PMY_MSG_LIST_ENTRY)(_MsgHead.Links.Flink);
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
	return IsListEmpty(&_MsgHead.Links);
}

VOID
removeMsgListFirst()
{
	KIRQL oldIrql;
	PLIST_ENTRY entry;

	if (IsListEmpty(&_MsgHead.Links)) {
		return;
	}

	KeAcquireSpinLock(&_SpinLock, &oldIrql);
	initMsgHead();
	entry = RemoveHeadList(&_MsgHead.Links);
	KeReleaseSpinLock(&_SpinLock, oldIrql);

	// 不释放内存， 内存由 MyCreateSection()　释放
}

VOID
setProcessIdOffset(
	ULONG Offset
	)
{
	ProcessIdOffset = Offset;
}