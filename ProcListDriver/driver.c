#include <ntifs.h>

#include "driver.h"
#include "process.h"

#define DRIVER_NAME L"ProcScanner"
#define SYMB_LINK_NAME L"\\DOSDevices\\" DRIVER_NAME L"CDOSL"
#define DEVICE_NAME L"\\Device\\" DRIVER_NAME L"CDO"

#define GET_DEVICE_EXT(_Device) \
	((PMY_DEVICE_EXT)(_Device->DeviceExtension))

static VOID init();

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
	__debugbreak();
#endif

	ULONG i;
	NTSTATUS status;
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
	DriverObject->MajorFunction[IRP_MJ_WRITE] = MyWriteDispatch;

	init();

	return STATUS_SUCCESS;
}

VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
	)
{
	PDEVICE_OBJECT device = DriverObject->DeviceObject;
	PMY_DEVICE_EXT ext = GET_DEVICE_EXT(device);

	NTSTATUS status = STATUS_SUCCESS;

	KdPrint((DBG_PREFIX"driver unloading...\r\n"));

	KdPrint((DBG_PREFIX"Symbolic link deleting...\r\n"));
	status = IoDeleteSymbolicLink(&ext->SymbLink);
	if (!NT_SUCCESS(status)) {
		KdPrint((DBG_PREFIX "deleting symb link failed #%x\r\n", status));
	}

	KdPrint((DBG_PREFIX "Device deleting...\r\n"));
	IoDeleteDevice(device);

	KdPrint((DBG_PREFIX "Free pool...\r\n"));
	freeListPool();
}

NTSTATUS
MyCreateDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	)
{
	UNREFERENCED_PARAMETER(DeviceObject);

	getProcessList();

	Irp->IoStatus.Status = STATUS_SUCCESS;
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

	MY_PROC_INFO procInfo;

	UNREFERENCED_PARAMETER(DeviceObject);

	irpsp = IoGetCurrentIrpStackLocation(Irp);
	bufferLen = irpsp->Parameters.Read.Length;

	buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, LowPagePriority);
	if (buffer == NULL) {
		status = STATUS_UNSUCCESSFUL;
		Irp->IoStatus.Status = status;
		Irp->IoStatus.Information = bufferLen;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	status = getProcInfoNext(&procInfo);

	if (NT_SUCCESS(status)) {
		buffer = writeBuffer(buffer, &procInfo.Pid, sizeof(ULONG));

		buffer = writeBuffer(buffer, &procInfo.PaPid, sizeof(ULONG));

		buffer = writeBuffer(buffer, &procInfo.ImageFileName, procInfo.nameBytes);

		writeBuffer(buffer, L"\0", sizeof(L"\0"));

		information = 2 * sizeof(ULONG) + procInfo.nameBytes + sizeof(L"\0");
	}
	else {
		status = STATUS_UNSUCCESSFUL;
	}

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
	PIO_STACK_LOCATION irpsp;
	NTSTATUS status = STATUS_SUCCESS;
	ULONG information = 0;
	PVOID buffer = NULL;
	ULONG bufferLen;

	PWCHAR name;

	UNREFERENCED_PARAMETER(DeviceObject);

	irpsp = IoGetCurrentIrpStackLocation(Irp);
	bufferLen = irpsp->Parameters.Write.Length;

	buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, LowPagePriority);
	if (buffer == NULL) {
		status = STATUS_UNSUCCESSFUL;
		Irp->IoStatus.Status = status;
		Irp->IoStatus.Information = bufferLen;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		return status;
	}

	buffer = readBuffer(buffer, &name, bufferLen);
	information = bufferLen;

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

PVOID
readBuffer(
	PVOID Buffer,
	PVOID Dst,
	SIZE_T Bytes
	)
{
	RtlCopyMemory(Dst, Buffer, Bytes);

	return (PUCHAR)Buffer + Bytes;
}

VOID
init()
//
// 只能被 DriverEntry 调用
// 通过 System 进程的 EPROCESS 找到 ActiveProcessHead
//
{
	PEPROCESS curproc;
	curproc = PsGetCurrentProcess();

	// System 进程前向指针所指即活动进程队列头
	setActiveProcessHead(GET_LIST_ENTRY_FROM_EPROCESS(curproc)->Blink);
}
