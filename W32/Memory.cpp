#include "Memory.h"
#include "../ClassDumper3.h"
std::vector<ProcessListItem> GetProcessList()
{
	std::vector<ProcessListItem> list;

	HANDLE hProcessSnap;
	PROCESSENTRY32 pe32;

	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
	{
		return list;
	}

	pe32.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(hProcessSnap, &pe32))
	{
		CloseHandle(hProcessSnap);
		return list;
	}

	do
	{
		ProcessListItem item;
		item.pid = pe32.th32ProcessID;
		item.name = pe32.szExeFile;

		HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pe32.th32ProcessID);
		if (hModuleSnap != INVALID_HANDLE_VALUE) {
			MODULEENTRY32 me32;
			me32.dwSize = sizeof(MODULEENTRY32);

			if (Module32First(hModuleSnap, &me32))
			{
				item.path = me32.szExePath; // Store the full path of the executable
			}

			CloseHandle(hModuleSnap);
		}
		
		if (IsSameBitsProcess(item.path))
		{
			list.push_back(item);
		}
		
		list.push_back(item);
	}
	while (Process32Next(hProcessSnap, &pe32));

	CloseHandle(hProcessSnap);
	return list;
}

std::vector<ProcessListItem> GetProcessList(std::string filter)
{
	std::vector<ProcessListItem> list;

	HANDLE hProcessSnap;
	PROCESSENTRY32 pe32;

	hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
	{
		return list;
	}

	pe32.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(hProcessSnap, &pe32))
	{
		CloseHandle(hProcessSnap);
		return list;
	}

	do
	{
		ProcessListItem item;
		item.pid = pe32.th32ProcessID;
		item.name = pe32.szExeFile;
		if (item.name.find(filter) != std::string::npos)
		{
			HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pe32.th32ProcessID);
			if (hModuleSnap != INVALID_HANDLE_VALUE)
			{
				MODULEENTRY32 me32;
				me32.dwSize = sizeof(MODULEENTRY32);

				if (Module32First(hModuleSnap, &me32))
				{
					item.path = me32.szExePath; // Store the full path of the executable
				}

				CloseHandle(hModuleSnap);
			}

			if (IsSameBitsProcess(item.path))
			{
				list.push_back(item);
			}
		}
	} while (Process32Next(hProcessSnap, &pe32));

	CloseHandle(hProcessSnap);
	return list;
}

bool Is32BitExecutable(const std::string& filePath, bool& bFailed)
{
	HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		bFailed = true;
		return false;
	}

	HANDLE hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
	if (!hMap)
	{
		CloseHandle(hFile);
		bFailed = true;
		return false;
	}

	LPVOID pFileBase = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
	if (!pFileBase)
	{
		bFailed = true;
		CloseHandle(hMap);
		CloseHandle(hFile);
		return false;
	}

	IMAGE_DOS_HEADER* pDosHeader = static_cast<IMAGE_DOS_HEADER*>(pFileBase);
	if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
	{
		bFailed = true;
		UnmapViewOfFile(pFileBase);
		CloseHandle(hMap);
		CloseHandle(hFile);
		return false;
	}

	IMAGE_NT_HEADERS* pNtHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(
		reinterpret_cast<char*>(pFileBase) + pDosHeader->e_lfanew);

	bool is64Bit = pNtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;

	UnmapViewOfFile(pFileBase);
	CloseHandle(hMap);
	CloseHandle(hFile);

	bFailed = false;
	return !is64Bit;
}

bool IsSameBitsProcess(const std::string& FilePath)
{
	bool b32Local = sizeof(void*) == 4;
	bool bFailed = false;
	bool b32Remote = Is32BitExecutable(FilePath, bFailed);
	return (b32Local == b32Remote) && !bFailed;
}

Process::Process(DWORD dwProcessID)
{
	this->dwProcessID = dwProcessID;
	hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwProcessID);
	if (hProcess == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::Log("Failed to open process");
	}
	CheckBits();

	char szProcessName[MAX_PATH] = "<unknown>";
	GetModuleBaseNameA(hProcess, NULL, szProcessName, sizeof(szProcessName) / sizeof(char));
	this->ProcessName = szProcessName;
}

Process::Process(const std::string& ProcessName)
{
	this->dwProcessID = GetProcessID(ProcessName);
	hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwProcessID);
	if (hProcess == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::Log("Failed to open process");
	}
	CheckBits();
	char szProcessName[MAX_PATH] = "<unknown>";
	GetModuleBaseNameA(hProcess, NULL, szProcessName, sizeof(szProcessName) / sizeof(char));
	this->ProcessName = szProcessName;
}

DWORD Process::GetProcessID(const std::string& ProcessName)
{
	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof(PROCESSENTRY32);
	HANDLE hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hProcessSnap == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::Log("Failed to create snapshot");

		return 0;
	}
	if (!Process32First(hProcessSnap, &pe32))
	{
		ClassDumper3::Log("Failed to get first process");
		CloseHandle(hProcessSnap);
		return 0;
	}
	do
	{
		if (ProcessName == pe32.szExeFile)
		{
			CloseHandle(hProcessSnap);
			return pe32.th32ProcessID;
		}
	} while (Process32Next(hProcessSnap, &pe32));
	
	CloseHandle(hProcessSnap);
	return 0;
}

bool Process::IsValid()
{
	return this->hProcess != INVALID_HANDLE_VALUE;
}

bool Process::Is32Bit()
{
	BOOL bIsWow64 = FALSE;
	typedef BOOL(WINAPI* LPFN_ISWOW64PROCESS) (HANDLE, PBOOL);
	HMODULE Kernel32 = GetModuleHandle(TEXT("kernel32"));

	if (Kernel32 == NULL)
	{
		ClassDumper3::Log("Failed to get kernel32 handle");
	}

	LPFN_ISWOW64PROCESS fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress((Kernel32), "IsWow64Process");
	if (fnIsWow64Process != nullptr)
	{
		fnIsWow64Process(this->hProcess, &bIsWow64);
		return bIsWow64;
	}
	ClassDumper3::Log("Error function IsWow64Process does not exist");
	return false;
}

bool Process::IsSameBits()
{
	bool b64Remote = !Is32Bit();
	bool b64Local = sizeof(void*) == 8;
	return b64Remote == b64Local;
}

bool Process::AttachDebugger()
{
	return DebugActiveProcess(this->dwProcessID);
}

bool Process::DetachDebugger()
{
	return DebugActiveProcessStop(this->dwProcessID);
}

bool Process::DebugWait()
{
	return WaitForDebugEvent(&this->debugEvent, INFINITE);
}

bool Process::DebugContinue(DWORD dwContinueStatus)
{
	return ContinueDebugEvent(this->debugEvent.dwProcessId, this->debugEvent.dwThreadId, dwContinueStatus);
}

void* Process::AllocRW(size_t size)
{
	return VirtualAllocEx(this->hProcess, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void* Process::AllocRWX(size_t size)
{
	return VirtualAllocEx(this->hProcess, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

bool Process::Free(void* Address)
{
	return VirtualFreeEx(this->hProcess, Address, 0, MEM_RELEASE);
}

void Process::CheckBits()
{
	if (!IsSameBits())
	{
		ClassDumper3::Log("Process is not the same bits");
	}
}


Process& Process::operator=(const Process& other)
{
	this->hProcess = other.hProcess;
	this->dwProcessID = other.dwProcessID;
	this->ProcessName = other.ProcessName;
	return *this;
}

Process::Process(const Process& other)
{
	this->hProcess = other.hProcess;
	this->dwProcessID = other.dwProcessID;
	this->ProcessName = other.ProcessName;
}

Process::Process(HANDLE hProcess, DWORD dwProcessID, const std::string& ProcessName)
{
	this->hProcess = hProcess;
	this->dwProcessID = dwProcessID;
	this->ProcessName = ProcessName;
}

Process::Process()
{
	this->hProcess = INVALID_HANDLE_VALUE;
	this->dwProcessID = NULL;
	this->ProcessName = "";
}

MemoryRange::MemoryRange(uintptr_t start, uintptr_t end, bool bExecutable, bool bReadable, bool bWritable)
{
	this->start = start;
	this->end = end;
	this->bExecutable = bExecutable;
	this->bReadable = bReadable;
	this->bWritable = bWritable;
}

bool MemoryRange::Contains(uintptr_t address) const
{
	return address >= start && address <= end;
}

uintptr_t MemoryRange::Size() const
{
	return end - start;
}

void MemoryMap::Setup(Process* process)
{
	// Get readable memory ranges
	MEMORY_BASIC_INFORMATION mbi;
	for (uintptr_t address = 0; VirtualQueryEx(process->hProcess, (LPCVOID)address, &mbi, sizeof(mbi)); address += mbi.RegionSize)
	{
		if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_READWRITE || mbi.Protect & PAGE_EXECUTE_READWRITE))
		{
			ranges.push_back(
				MemoryRange(
					(uintptr_t)mbi.BaseAddress,
					(uintptr_t)mbi.BaseAddress + mbi.RegionSize,
					mbi.Protect & PAGE_EXECUTE,
					mbi.Protect & PAGE_READONLY,
					mbi.Protect & PAGE_READWRITE));
		}
	}
	if (ranges.size() == 0)
	{
		ClassDumper3::LogF("Failed to get memory ranges error code: %u", GetLastError());

	}
	
	ClassDumper3::LogF("Found %u memory regions", ranges.size());
}

bool ModuleSection::Contains(uintptr_t address) const
{
	return address >= start && address <= end;
}

uintptr_t ModuleSection::Size() const
{
	return end - start;
}

void ModuleMap::Setup(Process* process)
{
	MODULEENTRY32 me32;
	me32.dwSize = sizeof(MODULEENTRY32);
	HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process->dwProcessID);
	if (hModuleSnap == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::Log("Failed to create snapshot");
		return;
	}
	if (!Module32First(hModuleSnap, &me32))
	{
		ClassDumper3::Log("Failed to get first module");
		CloseHandle(hModuleSnap);
		return;
	}
	do
	{
		modules.push_back(Module());
		Module& module = modules.back();
		module.baseAddress = me32.modBaseAddr;
		module.name = me32.szModule;
		IMAGE_DOS_HEADER dosHeader;
		ReadProcessMemory(process->hProcess, me32.modBaseAddr, &dosHeader, sizeof(dosHeader), nullptr);
		IMAGE_NT_HEADERS ntHeaders;
		ReadProcessMemory(process->hProcess, (void*)((uintptr_t)me32.modBaseAddr + dosHeader.e_lfanew), &ntHeaders, sizeof(ntHeaders), nullptr);
		IMAGE_SECTION_HEADER* sectionHeaders = new IMAGE_SECTION_HEADER[ntHeaders.FileHeader.NumberOfSections];
		ReadProcessMemory(process->hProcess, (void*)((uintptr_t)me32.modBaseAddr + dosHeader.e_lfanew + sizeof(IMAGE_NT_HEADERS)), sectionHeaders, sizeof(IMAGE_SECTION_HEADER) * ntHeaders.FileHeader.NumberOfSections, nullptr);
		for (int i = 0; i < ntHeaders.FileHeader.NumberOfSections; i++)
		{
			IMAGE_SECTION_HEADER& sectionHeader = sectionHeaders[i];
			module.sections.push_back(ModuleSection());
			ModuleSection& section = module.sections.back();
			section.name = (char*)sectionHeader.Name;
			section.start = (uintptr_t)me32.modBaseAddr + sectionHeader.VirtualAddress;
			section.end = section.start + sectionHeader.Misc.VirtualSize;
			section.bFlagExecutable = sectionHeader.Characteristics & IMAGE_SCN_MEM_EXECUTE;
			section.bFlagReadonly = sectionHeader.Characteristics & IMAGE_SCN_MEM_READ;
		}
	} while (Module32Next(hModuleSnap, &me32));
	CloseHandle(hModuleSnap);

	ClassDumper3::LogF("Found %d modules", modules.size());
}

Module* ModuleMap::GetModule(const char* name)
{
	auto it = std::find_if(modules.begin(), modules.end(), [name](const Module& module) { return module.name == name; });
	if (it != modules.end()) return &*it;
	return nullptr;
}

Module* ModuleMap::GetModule(const std::string& name)
{
	auto it = std::find_if(modules.begin(), modules.end(), [name](const Module& module) { return module.name == name; });
	if (it != modules.end()) return &*it;
	return nullptr;
}

Module* ModuleMap::GetModule(uintptr_t address)
{
	auto it = std::find_if(modules.begin(), modules.end(), [address](const Module& module)
		{
			return address >= (uintptr_t)module.baseAddress && address <= (uintptr_t)module.baseAddress + module.sections.back().end;
		});
	if (it != modules.end()) return &*it;
	return nullptr;
}

void TargetProcess::Setup(const std::string& processName)
{
	process = Process(processName);
	memoryMap.Setup(&process);
	moduleMap.Setup(&process);
}

void TargetProcess::Setup(DWORD processID)
{
	process = Process(processID);
	memoryMap.Setup(&process);
	moduleMap.Setup(&process);
}

void TargetProcess::Setup(Process process)
{
	this->process = process;
	memoryMap.Setup(&process);
	moduleMap.Setup(&process);
}

bool TargetProcess::Is64Bit()
{
	return !process.Is32Bit();
}

bool TargetProcess::IsValid()
{
	return process.IsValid();
}

Module* TargetProcess::GetModule(const std::string& moduleName)
{
	for (auto& module : moduleMap.modules)
	{
		if (module.name.find(moduleName) != std::string::npos)
		{
			return &module;
		}
	}
	return nullptr;
};

MemoryRange* TargetProcess::GetMemoryRange(uintptr_t address)
{
	for (auto& range : memoryMap.ranges)
	{
		if (range.Contains(address))
		{
			return &range;
		}
	}

	return nullptr;
}

std::vector<MemoryBlock> TargetProcess::GetReadableMemory()
{
	std::vector<std::future<MemoryBlock>> futures;
	std::vector<MemoryBlock> blocks;
	for (auto& range : memoryMap.ranges)
	{
		if (range.bReadable)
		{
			// get a future using async launch lambda
			auto future = std::async(std::launch::async, [&range, &process = process]()
				{
					MemoryBlock block;
					block.blockAddress = (void*)range.start;
					block.blockSize = range.Size();
					block.blockCopy = malloc(block.blockSize);
					ReadProcessMemory(process.hProcess, block.blockAddress, block.blockCopy, block.blockSize, NULL);
					return block;
				});
			futures.push_back(std::move(future));
		}
	}

	// wait for all futures to finish
	for (auto& future : futures)
	{
		blocks.push_back(future.get());
	}

	return blocks;
}

std::vector<std::future<MemoryBlock>> TargetProcess::AsyncGetReadableMemory()
{
	std::vector<std::future<MemoryBlock>> futures;
	for (auto& range : memoryMap.ranges)
	{
		if (range.bReadable)
		{
			// get a future using async launch lambda
			auto future = std::async(std::launch::async, [&range, &process = process]()
				{
					MemoryBlock block;
					block.blockAddress = (void*)range.start;
					block.blockSize = range.Size();
					block.blockCopy = malloc(block.blockSize);
					ReadProcessMemory(process.hProcess, block.blockAddress, block.blockCopy, block.blockSize, NULL);
					return block;
				});
			futures.push_back(std::move(future));
		}
	}
	return futures;
}

ModuleSection* TargetProcess::GetModuleSection(uintptr_t address)
{
	for (auto& module : moduleMap.modules)
	{
		for (auto& section : module.sections)
		{
			if (section.Contains(address))
			{
				return &section;
			}
		}
	}
	return nullptr;
}


DWORD TargetProcess::SetProtection(uintptr_t address, size_t size, DWORD protection)
{
	DWORD oldProtection;
	VirtualProtectEx(process.hProcess, (void*)address, size, protection, &oldProtection);
	return oldProtection;
}

std::vector<HWND> TargetProcess::GetWindows()
{
	std::vector<HWND> windowsTemp;
	std::vector<HWND> windowsFinal;
	EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL
		{
			((std::vector<HWND>*)lParam)->push_back(hwnd);
			return TRUE;
		}, (LPARAM)&windowsTemp);

	for (auto& window : windowsTemp)
	{
		DWORD processID;
		GetWindowThreadProcessId(window, &processID);
		if (processID == process.dwProcessID)
		{
			windowsFinal.push_back(window);
		}
	}

	return windowsFinal;
}

std::string TargetProcess::GetWindowName(HWND window)
{
	char buffer[256];
	GetWindowTextA(window, buffer, 256);
	return std::string(buffer);
}

bool TargetProcess::SetWindowName(HWND window, const std::string& name)
{
	return SetWindowTextA(window, name.c_str());
}

bool TargetProcess::SetTransparency(HWND window, BYTE alpha)
{
	return SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA);
}

void TargetProcess::Read(uintptr_t address, void* buffer, size_t size)
{
	if (!ReadProcessMemory(process.hProcess, (void*)address, buffer, size, NULL))
	{
		printf("ReadProcessMemory failed: %d\n", GetLastError());
	}
}

std::future<void*> TargetProcess::AsyncRead(uintptr_t address, size_t size)
{
	return std::async(std::launch::async, [this, address, size]()
		{
			void* buffer = malloc(size);
			ReadProcessMemory(process.hProcess, (void*)address, buffer, size, NULL);
			return buffer;
		});
}

void TargetProcess::Write(uintptr_t address, void* buffer, size_t size)
{
	WriteProcessMemory(process.hProcess, (void*)address, buffer, size, NULL);
}

void TargetProcess::AsyncWrite(uintptr_t address, void* buffer, size_t size)
{
	auto result = std::async(std::launch::async, [this, address, buffer, size]()
		{
			WriteProcessMemory(process.hProcess, (void*)address, buffer, size, NULL);
		});
}

HANDLE TargetProcess::InjectDLL(const std::string& dllPath)
{
	HANDLE hThread = NULL;
	LPVOID LoadLibraryAAddr = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
	LPVOID RemoteString = (LPVOID)VirtualAllocEx(process.hProcess, NULL, dllPath.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	WriteProcessMemory(process.hProcess, (LPVOID)RemoteString, dllPath.c_str(), dllPath.size(), NULL);
	hThread = CreateRemoteThread(process.hProcess, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibraryAAddr, (LPVOID)RemoteString, NULL, NULL);
	return hThread;
}

void TargetProcess::InjectDLLAsync(const std::string& dllPath)
{
	auto result = std::async(std::launch::async, [this, dllPath]()
		{
			InjectDLL(dllPath);
		});
}