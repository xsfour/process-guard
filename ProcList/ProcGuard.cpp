#include "ProcGuard.h"



ProcGuard::ProcGuard()
{
}


ProcGuard::~ProcGuard()
{
}


BOOLEAN ProcGuard::listen(const std::string& targetName)
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

	UCHAR buffer[2050] = { 0 };
	ULONG length;

	RtlCopyMemory(buffer, targetName.c_str(), targetName.size());
	WriteFile(device, buffer,
		targetName.size(),
		&length, NULL);

	printf("监听 %s*\r\n", targetName.c_str());
	listening = TRUE;

	int i = 0;
	while (listening)
	{
		if (!ReadFile(device, buffer,
			sizeof(buffer) - sizeof(WCHAR), &length, NULL)) {
			if (!listening) {
				break;
			}
			if (++i == 6) {
				printf("\r        \r");
				i = 0;
			}
			printf(".");
			Sleep(500);
			continue;
		}

		printf("\r\n");

		ULONG pid = *(PULONG)(buffer);
		std::wstring imageFilename =
			(PWCHAR)(buffer + sizeof(ULONG));

		printf("进程 %6lu 加载 %ws\r\n",
			pid,
			imageFilename.c_str()
			);
	}

	CloseHandle(device);

	return TRUE;
}

VOID ProcGuard::stop()
{
	listening = FALSE;
}

const std::wstring& ProcGuard::getLastError()
{
	return errorMsg;
}
