#ifndef USER_MODE_QUERY_H
#define USER_MODE_QUERY_H

#include <string>
#include <vector>
#include <iterator>

#include <Windows.h>
#include <psapi.h>

#include "ProcessInfo.h"

class UserModeQuery {
public:
	inline static UserModeQuery &getInstance();

	~UserModeQuery() {}

	inline Processes& getProcesses();

	const std::wstring& getLastError() const
	{
		return errorMsg;
	}

	std::wstring errorMsg;

private:
	UserModeQuery() {}

	Processes processes;

	inline PWCHAR getModuleName(ULONG pid);
};

UserModeQuery&
UserModeQuery::getInstance()
{
	static UserModeQuery instance;

	return instance;
}

inline Processes& UserModeQuery::getProcesses()
{
	processes.clear();

	ULONG aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;

	if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
	{
		errorMsg = L"获取进程列表时错误";
		return processes;
	}

	cProcesses = cbNeeded / sizeof(ULONG);

	ProcessInfo info;
	for (i = 0; i < cProcesses; i++)
	{
		if (aProcesses[i] != 0)
		{
			info.Pid = aProcesses[i];
			info.ImageName =
				getInstance().getModuleName(aProcesses[i]);
		}

		processes.push_back(info);
	}

	return processes;
}


inline PWCHAR
UserModeQuery::getModuleName(ULONG processID)
{
	static WCHAR szProcessName[MAX_PATH] = TEXT("<unknown>");

	HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION |
		PROCESS_VM_READ,
		FALSE, processID);

	// Get the process name.
	if (NULL != hProcess)
	{
		HMODULE hMod;
		ULONG cbNeeded;

		if (EnumProcessModules(hProcess, &hMod, sizeof(hMod),
			&cbNeeded))
		{
			GetModuleBaseName(hProcess, hMod, szProcessName,
				sizeof(szProcessName) / sizeof(WCHAR));
		}
	}

	CloseHandle(hProcess);

	return szProcessName;
}

#endif // !USER_MODE_QUERY_H