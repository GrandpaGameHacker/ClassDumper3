#include "Processes.h"
#include <psapi.h>
std::vector<PROCESSENTRY32*> GetProcessList()
{
    std::vector<PROCESSENTRY32*> ProcessList;
	HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
	{
		return ProcessList;
	}
	
	auto ProcessEntry = new PROCESSENTRY32;
	ProcessEntry->dwSize = sizeof(PROCESSENTRY32);
	if (!Process32First(hProcessSnap, ProcessEntry)) {
		delete ProcessEntry;
		return ProcessList;
	}
	while(true)
	{
		if (ProcessEntry) {
			HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, ProcessEntry->th32ProcessID);
			if (hProc != INVALID_HANDLE_VALUE && IsSupportedBits(hProc) && !IsSystemProcess(hProc))
			{
				ProcessList.push_back(ProcessEntry);
			}
			CloseHandle(hProc);
		}
		else {
			delete ProcessEntry;
		}

		ProcessEntry = new PROCESSENTRY32;
		ProcessEntry->dwSize = sizeof(PROCESSENTRY32);
		if (!Process32Next(hProcessSnap, ProcessEntry)) {
			delete ProcessEntry;
			return ProcessList;
		}
	}
}

std::vector<MODULEENTRY32*> GetModuleList(DWORD dwProcessID)
{
	auto moduleList = std::vector<MODULEENTRY32*>();
	HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, dwProcessID);
	if (hModuleSnap == INVALID_HANDLE_VALUE)
	{
		return moduleList;
	}
	auto* ModuleEntry = new MODULEENTRY32;
	ModuleEntry->dwSize = sizeof(MODULEENTRY32);
	if (!Module32First(hModuleSnap, ModuleEntry))
	{
		delete ModuleEntry;
		return moduleList;
	}
	while (true)
	{
		if (ModuleEntry && !IsSystemModule(ModuleEntry))
		{
			moduleList.push_back(ModuleEntry);
		}
		else
		{
			delete ModuleEntry;
		}

		ModuleEntry = new MODULEENTRY32;
		ModuleEntry->dwSize = sizeof(MODULEENTRY32);
		if (!Module32Next(hModuleSnap, ModuleEntry))
		{
			delete ModuleEntry;
			return moduleList;
		}
	}
}

bool IsSystemModule(MODULEENTRY32* Module)
{
	std::string modstr = std::string(Module->szExePath);
	std::transform(modstr.begin(), modstr.end(), modstr.begin(),
		[](unsigned char c) { return std::tolower(c); });
	if (modstr.find("\\windows\\") != std::string::npos)
	{
		return true;
	}
	else {
		return false;
	}
}

bool IsSystemProcess(HANDLE hProcess)
{
	HANDLE processHandle = NULL;
	TCHAR filename[MAX_PATH];
	if (hProcess != INVALID_HANDLE_VALUE) {
		if (GetModuleFileNameEx(hProcess, NULL, filename, MAX_PATH) != 0) {
			std::string modstr = std::string(filename);
			std::transform(modstr.begin(), modstr.end(), modstr.begin(),
				[](unsigned char c) { return std::tolower(c); });
			if (modstr.find("\\windows\\") != std::string::npos)
			{
				return true;
			}
			else {
				return false;
			}
		}
		
	}
	return false; //failure, default to false
}

bool IsSupportedBits(HANDLE hProcess)
{
	BOOL b32;
	IsWow64Process(hProcess, &b32);
#ifdef _WIN64
	return !b32;
#else
	return b32;
#endif
}

SectionInfo* GetSectionInfo(HANDLE hProcess, MODULEENTRY32* Module)
{
	auto* sectionInfo = new SectionInfo;
	memset(static_cast<void*>(sectionInfo), 0, sizeof(SectionInfo));

	void* moduleBase = Module->modBaseAddr;
	BYTE* moduleData = new BYTE[Module->modBaseSize];

	SIZE_T bytesRead;
	ReadProcessMemory(hProcess, moduleBase, (void*)moduleData, Module->modBaseSize, &bytesRead);

	if (bytesRead != Module->modBaseSize) {
		delete[] moduleData;
		delete sectionInfo;
		return nullptr;
	}

	auto* DosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(moduleData);
	if (!DosHeader || DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
	{
		delete[] moduleData;
		delete sectionInfo;
		return nullptr;
	}
	auto* NTHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(moduleData + DosHeader->e_lfanew);
	if (!NTHeader || NTHeader->Signature != IMAGE_NT_SIGNATURE)
	{
		delete sectionInfo;
		return nullptr;
	}
	const WORD NumberOfSections = NTHeader->FileHeader.NumberOfSections;
	IMAGE_SECTION_HEADER* Section = IMAGE_FIRST_SECTION(NTHeader);
	bool TEXT_found = false;
	bool RDATA_found = false;
	for (WORD i = 0; i < NumberOfSections; i++)
	{
		if (strcmp(reinterpret_cast<char const*>(Section[i].Name), ".text") == 0)
		{
			sectionInfo->TEXT.base = Section[i].VirtualAddress + reinterpret_cast<uintptr_t>(moduleBase);
			sectionInfo->TEXT.size = Section[i].SizeOfRawData;
			sectionInfo->TEXT.end = sectionInfo->TEXT.base + sectionInfo->TEXT.size - 1;
			TEXT_found = true;
			continue;
		}
		if (strcmp(reinterpret_cast<char const*>(Section[i].Name), ".rdata") == 0)
		{
			sectionInfo->RDATA.base = Section[i].VirtualAddress + reinterpret_cast<uintptr_t>(moduleBase);
			sectionInfo->RDATA.size = Section[i].SizeOfRawData;
			sectionInfo->RDATA.end = sectionInfo->RDATA.base + sectionInfo->RDATA.size - 1;
			RDATA_found = true;
		}
		if (TEXT_found && RDATA_found)
			break;
	}
	if (sectionInfo->TEXT.base && sectionInfo->RDATA.base)
	{
		return sectionInfo;
	}
	delete sectionInfo;
	return nullptr;
}
bool IsProcessAlive(HANDLE hProcess)
{
	if (WaitForSingleObject(hProcess, 0) == WAIT_TIMEOUT) {
		return true;
	}
	return false;
};

//alive = kernel32.WaitForSingleObject(self.handle, 0)
//if alive == winnt.WAIT_TIMEOUT:
	//return True
//else:
	//return False

