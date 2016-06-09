#pragma once

#include <string>

#include "Windows.h"

class ProcGuard
{
public:
	~ProcGuard();

	inline static ProcGuard& getInstance();

	BOOLEAN listen();

private:
	ProcGuard();

	HANDLE device;

	std::wstring errorMsg;

	const WCHAR *DEVICE_SYMB_LINK = L"\\\\.\\ProcGuardCDOSL";
};


ProcGuard& ProcGuard::getInstance()
{
	static ProcGuard instance;

	return instance;
}
