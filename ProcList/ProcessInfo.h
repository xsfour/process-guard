#ifndef PROCESS_INFO_H
#define PROCESS_INFO_H

#include <string>
#include <vector>
#include <iterator>

#include <Windows.h>

typedef struct _ProcessInfo {
	ULONG Pid;
	ULONG PaPid;
	std::wstring ImageName;
	std::wstring ImageFileName;
	BOOLEAN Hidden;

	_ProcessInfo(BOOLEAN hidden = FALSE) :
		Pid(0), PaPid(0), ImageName(L""),
		ImageFileName(L""), Hidden(hidden)
	{
		//
	}

	static int ProcessInfoCompare(const _ProcessInfo &a, const _ProcessInfo &b)
	{
		return a.Pid < b.Pid;
	}
}ProcessInfo, *PProcessInfo;

typedef std::vector<_ProcessInfo> Processes;
typedef std::vector<_ProcessInfo>::iterator ProcessIterator;
typedef std::vector<_ProcessInfo>::const_iterator ProcessConstIterator;

#endif // !PROCESS_INFO_H
