#pragma once

#include <Windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <TlHelp32.h>

struct Section
{
	uintptr_t base;
	uintptr_t end;
	size_t size;
};

struct SectionInfo
{
	uintptr_t ModuleBase;
	Section TEXT;
	Section RDATA;
};

std::vector<PROCESSENTRY32*> GetProcessList();
std::vector<MODULEENTRY32*> GetModuleList(DWORD dwProcessID);
SectionInfo* GetSectionInfo(HANDLE hProcess, MODULEENTRY32* Module);
bool IsSystemProcess(HANDLE hProcess);
bool IsSystemModule(MODULEENTRY32*);
bool IsSupportedBits(HANDLE hProcess);
bool IsProcessAlive(HANDLE hProcess);