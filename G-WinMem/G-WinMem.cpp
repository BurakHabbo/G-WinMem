// G-WinMem.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <Windows.h>
#include <iostream>
#include <stdlib.h>
#include <string>
#include <iterator>
#include <iphlpapi.h>
#include <psapi.h>
#include <Tlhelp32.h>
#include <WbemIdl.h>
#include <winternl.h>
#include <comdef.h>
#include <vector>
#include <filesystem>
#include <thread>

#include "Process.h"
#include <future>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Ntdll.lib")


PVOID GetPebAddress(HANDLE pHandle)
{
	PROCESS_BASIC_INFORMATION pbi;
	NtQueryInformationProcess(pHandle, ProcessBasicInformation, &pbi, sizeof(pbi), nullptr);

	return pbi.PebBaseAddress;
}

bool IsFlashProcess(int pid)
{
	PPROCESS_BASIC_INFORMATION pbi = nullptr;
	PEB peb = { NULL };
	RTL_USER_PROCESS_PARAMETERS processParams = { NULL };

	auto hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (hProcess == INVALID_HANDLE_VALUE) {
		std::cerr << "Invalid process handle\n";
		return false;
	}

	auto heap = GetProcessHeap();
	auto pbiSize = sizeof(PROCESS_BASIC_INFORMATION);

	pbi = static_cast<PPROCESS_BASIC_INFORMATION>(HeapAlloc(heap, HEAP_ZERO_MEMORY, pbiSize));

	if (!pbi)
	{
		CloseHandle(hProcess);
		return false;
	}

	auto pebAddr = GetPebAddress(hProcess);

	SIZE_T bytesRead;
	if (ReadProcessMemory(hProcess, pebAddr, &peb, sizeof(peb), &bytesRead))
	{
		bytesRead = 0;
		if (ReadProcessMemory(hProcess, peb.ProcessParameters, &processParams, sizeof(RTL_USER_PROCESS_PARAMETERS), &bytesRead))
		{
			if (processParams.CommandLine.Length > 0)
			{
				auto buffer = static_cast<WCHAR *>(malloc(processParams.CommandLine.Length * sizeof(WCHAR)));

				if (buffer)
				{
					if (ReadProcessMemory(hProcess, processParams.CommandLine.Buffer, buffer, processParams.CommandLine.Length, &bytesRead))
					{
						const _bstr_t b(buffer);
						if (strstr(static_cast<char const *>(b), "ppapi") || strstr(static_cast<char const *>(b), "plugin-container"))
						{
							CloseHandle(hProcess);
							HeapFree(heap, 0, pbi);
							return true;
						}			
					}
				}
				free(buffer);
			}
		}
	}

	CloseHandle(hProcess);
	HeapFree(heap, 0, pbi);

	return false;
}

std::vector<int> GetProcessId()
{
	std::vector<int> processIds;

	DWORD aProcesses[1024], cbNeeded, cProcesses;
	unsigned int i;

	if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
	{
		return processIds;
	}

	cProcesses = cbNeeded / sizeof(DWORD);

	for (i = 0; i < cProcesses; i++)
	{
		if (aProcesses[i] != 0)
		{
			processIds.push_back(aProcesses[i]);
		}
	}
	
	return processIds;
}

int main(int argc, char **argv)
{	
	auto pids = GetProcessId();
	std::vector<std::thread> threads;

	for (auto pid : pids) {
		if (IsFlashProcess(pid)) {
			auto p = new Process(pid);
			threads.push_back(std::thread (std::bind(&Process::PrintRC4Possibilities, p)));
		}
	}

	for (auto i = 0; i < threads.size(); i++)
		if (threads[i].joinable())
			threads[i].join();

	if (pids.empty())
		std::cerr << "No pids found\n";

    return 0;
}