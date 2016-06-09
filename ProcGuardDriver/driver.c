#include "driver.h"

#include "message.h"
#include "my_ssdt.h"

#define DRIVER_NAME L"ProcGuard"
#define SYMB_LINK_NAME L"\\DOSDevices\\" DRIVER_NAME L"CDOSL"
#define DEVICE_NAME L"\\Device\\" DRIVER_NAME L"CDO"

#define GET_DEVICE_EXT(_Device) \
	((PMY_DEVICE_EXT)(_Device->DeviceExtension))


static KSPIN_LOCK _SpinLock;

static BOOLEAN _DeviceCreated = FALSE;

static ULONG
getProcessIdOffset();

static PVOID
writeBuffer(
	PVOID Buffer,
	PVOID Src,
	SIZE_T Bytes
	);

static PVOID
readBuffer(
	PVOID Buffer,
	PVOID Dst,
	SIZE_T Bytes
	);

NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
	)
{
#if DBG
	// ProcListDriver::DriverEntry()
	__debugbreak();
#endif

	ULONG i;
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT device;
	PMY_DEVICE_EXT deviceExt;

	UNICODE_STRING deviceName =
		RTL_CONSTANT_STRING(DEVICE_NAME);

	UNREFERENCED_PARAMETER(RegistryPath);

	// 创建设备
	status = IoCreateDevice(
		DriverObject,
		sizeof(MY_DEVICE_EXT),
		&deviceName,
		FILE_DEVICE_UNKNOWN,
		0,
		FALSE,
		&device);

	if (!NT_SUCCESS(status)) {
		return status;
	}

	deviceExt = GET_DEVICE_EXT(device);
	RtlInitUnicodeString(&deviceExt->SymbLink, SYMB_LINK_NAME);

	status = IoCreateSymbolicLink(
		&deviceExt->SymbLink,
		&deviceName);

	if (!NT_SUCCESS(status)) {
		KdPrint((DBG_PREFIX "Symblic link creating failed #%x", status));
		IoDeleteDevice(device);
		return status;
	}

	device->Flags &= ~DO_DEVICE_INITIALIZING;
	device->Flags |= DO_DIRECT_IO;

	// 卸载函数
	DriverObject->DriverUnload = DriverUnload;

	// 分发函数
	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; ++i) {
		DriverObject->MajorFunction[i] = MyNullDispatch;
	}
	DriverObject->MajorFunction[IRP_MJ_CREATE] = MyCreateDispatch;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = MyCloseDispatch;
	DriverObject->MajorFunction[IRP_MJ_READ] = MyReadDispatch;

	//setProcessIdOffset(getProcessIdOffset());

	return status;
}

VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
	)
{
	PDEVICE_OBJECT device = DriverObject->DeviceObject;
	PMY_DEVICE_EXT ext = GET_DEVICE_EXT(device);

	NTSTATUS status = STATUS_SUCCESS;

	KdPrint((DBG_PREFIX "driver unloading...\r\n"));

	KdPrint((DBG_PREFIX "Symbolic link deleting...\r\n"));
	status = IoDeleteSymbolicLink(&ext->SymbLink);
	if (!NT_SUCCESS(status)) {
		KdPrint((DBG_PREFIX "deleting symb link failed #%x\r\n", status));
	}

	KdPrint((DBG_PREFIX "Device deleting...\r\n"));
	IoDeleteDevice(device);
}

NTSTATUS
MyCreateDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	)
{
	KIRQL oldIrql;

	UNREFERENCED_PARAMETER(DeviceObject);

	if (_DeviceCreated) {
		goto failed;
	}

	KeAcquireSpinLock(&_SpinLock, &oldIrql);
	if (_DeviceCreated) {
		goto failed;
	}
	_DeviceCreated = TRUE;
	KeReleaseSpinLock(&_SpinLock, oldIrql);
	
	// 当设备创建时进行挂钩
	hook();

	Irp->IoStatus.Status = STATUS_SUCCESS;

	goto ret;
failed:
	Irp->IoStatus.Status = STATUS_ACCESS_DENIED;

ret:
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;
}

NTSTATUS
MyCloseDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	_DeviceCreated = FALSE;  // Safe?

	// 设备关闭时取消挂钩
	KdPrint((DBG_PREFIX "Unhooking...\r\n"));
	unhook();

	return Irp->IoStatus.Status;
}

NTSTATUS
MyReadDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	)
{
	PIO_STACK_LOCATION irpsp;
	NTSTATUS status = STATUS_SUCCESS;
	ULONG information = 0;
	PVOID buffer = NULL;
	ULONG bufferLen;
	PMY_MSG_LIST_ENTRY msg;
	PVOID eventAddr;

	UNREFERENCED_PARAMETER(DeviceObject);

	irpsp = IoGetCurrentIrpStackLocation(Irp);
	bufferLen = irpsp->Parameters.Read.Length;

	buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, LowPagePriority);
	if (buffer == NULL) {
		status = STATUS_UNSUCCESSFUL;
		information = bufferLen;
		goto ret;
	}

	// 如果消息队列是空的，等待新的消息产生
	while (isMsgListEmpty()) {
		KeWaitForSingleObject(
			getNewMsgEvent(),
			Executive,
			KernelMode,
			0, NULL);
	}

	msg = queryMsgListFirst();  // 理论上不会出现结果为 NULL 的情况？

	eventAddr = &msg->Event;
	buffer = writeBuffer(buffer, &msg->Pid, sizeof(msg->Pid));
	buffer = writeBuffer(buffer, &eventAddr, sizeof(eventAddr));
	buffer = writeBuffer(buffer, msg->Filename.Buffer, msg->Filename.Length);
	information = sizeof(eventAddr) + sizeof(msg->Pid) + msg->Filename.Length;

ret:
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = information;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;
}

NTSTATUS
MyWriteDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	ULONG information = 0;
	PIO_STACK_LOCATION irpsp;
	PVOID buffer;
	ULONG bufferLen;

	PKEVENT pEvent;
	USHORT response;
	PMY_MSG_LIST_ENTRY msg;

	UNREFERENCED_PARAMETER(DeviceObject);

	irpsp = IoGetCurrentIrpStackLocation(Irp);
	bufferLen = irpsp->Parameters.Read.Length;

	buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, LowPagePriority);
	if (buffer == NULL) {
		status = STATUS_UNSUCCESSFUL;
		information = bufferLen;
		goto ret;
	}

	// 读取内容
	buffer = readBuffer(buffer, &pEvent, sizeof(pEvent));
	buffer = readBuffer(buffer, &response, sizeof(response));
	information = sizeof(pEvent) + sizeof(response);

	// 根据用户的反馈处理相应消息
	msg = findMsgByEvent(pEvent);
	msg->Status =(response == MSG_STATUS_CONFIRMED) ?
		MSG_STATUS_CONFIRMED : MSG_STATUS_ABANDONED;
	KeSetEvent(&msg->Event, 0, FALSE);

ret:
	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = information;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;
}

NTSTATUS
MyNullDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
	Irp->IoStatus.Information = 0;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	return Irp->IoStatus.Status;
}

ULONG
getProcessIdOffset()
{
	ULONG i;
	ULONG offset = 0;

	PULONG ptr = (PULONG)PsGetCurrentProcess();

	for (i = 0; i < 512; ++i) {
		if (ptr[i] == 0x4) {	// System 进程的 PID
			offset = i;
			break;
		}
	}

	KdPrint((DBG_PREFIX "PID offset = %x\r\n", offset));

	return offset;
}

PVOID
writeBuffer(
	PVOID Buffer,
	PVOID Src,
	SIZE_T Bytes
	)
{
	RtlCopyMemory(Buffer, Src, Bytes);

	return (PUCHAR)Buffer + Bytes;
}


static PVOID
readBuffer(
	PVOID Buffer,
	PVOID Dst,
	SIZE_T Bytes
	)
{
	RtlCopyMemory(Dst, Buffer, Bytes);

	return (PUCHAR)Buffer + Bytes;
}