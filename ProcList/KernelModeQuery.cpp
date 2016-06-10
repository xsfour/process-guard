#include "KernelModeQuery.h"

#include <string>

#include <stdio.h>
#include <algorithm>

BOOLEAN KernelModeQuery::reloadProcesses()
{
	processes.clear();

	HANDLE device = CreateFile(
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
	ProcessInfo info(TRUE);

	int i = 0;
	while (ReadFile(device, buffer,
		sizeof(buffer) - sizeof(WCHAR),&length, NULL))
	{
		info.Pid = *(PULONG)(buffer);
		info.PaPid = *(PULONG)(buffer + sizeof(ULONG));
		info.ImageFileName =
			(PWCHAR)(buffer + 2 * sizeof(ULONG));

		size_t idx = info.ImageFileName.find_last_of(L'\\');
		if (idx == std::wstring::npos) {
			info.ImageName = info.ImageFileName;
		}
		else {
			info.ImageName = info.ImageFileName.substr(++idx);
		}

		processes.push_back(info);
	}

	CloseHandle(device);

	return TRUE;
}

VOID
KernelModeQuery::mergeProcesses(Processes &newProcesses)
{
	sort(processes);
	sort(newProcesses);

	Processes::iterator it1 = processes.begin();
	Processes::iterator it2 = newProcesses.begin();

	while (it1 != processes.end() && it2 != newProcesses.end()) {
		if (it1->Pid == 4) {
			it1->ImageName = L"System";
		}
		if (it1->Pid == it2->Pid) {
			it1->Hidden = FALSE;
			++it1;
			++it2;
		}
		else if (it1->Pid < it2->Pid) {
			++it1;
		}
		else {
			++it2;
		}
	}
}

const Processes&
KernelModeQuery::getProcesses() const
{
	return processes;
}

const std::wstring&
KernelModeQuery::getLastError() const
{
	return errorMsg;
}

VOID
KernelModeQuery::sort(Processes &processes)
{
	std::sort(processes.begin(), processes.end(),
		ProcessInfo::ProcessInfoCompare);
}

BOOLEAN
KernelModeQuery::hideProcess(const std::string &name)
{
	HANDLE device = CreateFile(
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

	char buffer[15];
	ULONG length;
	size_t size = min(15, name.size() + 1);

	for (size_t i = 0; i < size; ++i) {
		buffer[i] = name[i];
	}
	buffer[size] = '\0';

	BOOL res = WriteFile(
		device,
		buffer,
		size,
		&length, NULL);

	return res;
}
