#include "ProcGuard.h"



ProcGuard::ProcGuard()
{
}


ProcGuard::~ProcGuard()
{
}


BOOLEAN ProcGuard::listen()
{
	device = CreateFile(
		DEVICE_SYMB_LINK,
		GENERIC_READ | GENERIC_WRITE,
		0,
		0,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_SYSTEM,
		0);

	if (device == INVALID_HANDLE_VALUE) {
		errorMsg = L"驱动未加载";
		return FALSE;
	}

	UCHAR buffer[2050];
	ULONG length;

	int i = 0;
	while (ReadFile(device, buffer,
		sizeof(buffer) - sizeof(WCHAR), &length, NULL))
	{
		ULONG pid = *(PULONG)(buffer);
		ULONG eventAddr = *(PULONG)(buffer + sizeof(ULONG));
		std::wstring imageFilename =
			(PWCHAR)(buffer + 2 * sizeof(ULONG));

		printf("是否允许：\r\n%lu, %08x, %ws\r\n",
			pid,
			eventAddr,
			imageFilename.c_str()
			);

		USHORT res;
		scanf_s("%hu", &res);

		RtlCopyMemory(buffer, &eventAddr, sizeof(ULONG));
		RtlCopyMemory((buffer + sizeof(ULONG)), &res, sizeof(res));
		WriteFile(device, buffer,
			sizeof(ULONG) + sizeof(USHORT),
			&length, NULL);

		if (res == 0) {
			break;
		}
	}

	CloseHandle(device);

	return TRUE;
}
