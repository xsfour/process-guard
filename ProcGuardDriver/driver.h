#pragma once

#include <ntifs.h>

typedef struct _MY_DEVICE_EXT {
	PULONG Size;
	UNICODE_STRING DeviceName;
	UNICODE_STRING SymbLink;
}MY_DEVICE_EXT, *PMY_DEVICE_EXT;


NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
	);

VOID
DriverUnload(
	_In_ PDRIVER_OBJECT DriverObject
	);

NTSTATUS
MyCreateDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	);

NTSTATUS
MyCloseDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	);

NTSTATUS
MyReadDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	);

NTSTATUS
MyWriteDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	);

NTSTATUS
MyNullDispatch(
	_In_ PDEVICE_OBJECT DeviceObject,
	_In_ PIRP Irp
	);

VOID init();
