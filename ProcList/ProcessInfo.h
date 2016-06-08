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
		ImageFileName(L""), Hidden(FALSE)
	{
		//
	}
	/*
	~_ProcessInfo()
	{
		delete[] ImageName;
		delete[] ImageFileName;
	}
	
	VOID setImageFileName(PWCHAR str, ULONG bytes)
	{
		if (ImageFileName != NULL) {
			delete[] ImageFileName;
		}

		ULONG len = (bytes + 1) / sizeof(WCHAR);

		ImageFileName = new WCHAR[len + 1];
		RtlCopyMemory(ImageFileName, str, bytes);
		ImageFileName[len] = L'\0';

		int i;
		for (i = (int)len - 1; i >= 0; --i) {
			if (ImageFileName[i] == L'\\') {
				break;
			}
		}
		len -= ++i;
		setImageName(ImageFileName + i, len * sizeof(WCHAR));
	}

	VOID setImageName(PWCHAR str, ULONG bytes)
	{
		if (ImageName != NULL) {
			delete[] ImageName;
		}

		ULONG len;

		if (bytes == 0) {
			len = lstrlenW(str);
		}
		else {
			len = (bytes + sizeof(WCHAR) / 2) / sizeof(WCHAR);
		}

		ImageName = new WCHAR[len + 1];
		RtlCopyMemory(ImageName, str, len * sizeof(WCHAR));
		ImageName[len] = L'\0';
	}
	*/
	static int ProcessInfoCompare(const _ProcessInfo &a, const _ProcessInfo &b)
	{
		return a.Pid < b.Pid;
	}
}ProcessInfo, *PProcessInfo;

typedef std::vector<_ProcessInfo> Processes;
typedef std::vector<_ProcessInfo>::iterator ProcessIterator;
typedef std::vector<_ProcessInfo>::const_iterator ProcessConstIterator;

#endif // !PROCESS_INFO_H
