#include <Windows.h>
#include <stdio.h>

#include "KernelModeQuery.h"
#include "UserModeQuery.h"
#include "ProcessInfo.h"
#include "ProcGuard.h"

int main(void)
{
	KernelModeQuery kernel = KernelModeQuery::getInstance();
	UserModeQuery user = UserModeQuery::getInstance();

	/*
	Processes procs = user.getProcesses();
	kernel.reloadProcesses();
	kernel.mergeProcesses(procs);
	procs = kernel.getProcesses();

	if (procs.empty()) {
		printf("NULL\r\n");
	}

	ProcessConstIterator it = procs.begin();
	while (it != procs.end()) {
		wprintf(L"%c %ws %-8lu %-8lu  %ws\r\n",
			it->Hidden ? '*' : ' ',
			it->ImageName.c_str(),
			it->Pid,
			it->PaPid,
			it->ImageFileName.c_str()
			);

		++it;
	}
	*/

	ProcGuard guard = ProcGuard::getInstance();
	guard.listen();

	system("pause");
	return 0;
}