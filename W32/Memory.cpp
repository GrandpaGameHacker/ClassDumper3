#include "Memory.h"
#include "../ClassDumper3.h"

std::vector<FProcessListItem> GetProcessList()
{
	std::vector<FProcessListItem> ProcessList;

	HANDLE ProcessSnapshotHandle;
	PROCESSENTRY32 ProcessEntry;

	ProcessSnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (ProcessSnapshotHandle == INVALID_HANDLE_VALUE)
	{
		return ProcessList;
	}

	ProcessEntry.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(ProcessSnapshotHandle, &ProcessEntry))
	{
		CloseHandle(ProcessSnapshotHandle);
		return ProcessList;
	}

	do
	{
		FProcessListItem ProcessItem;
		ProcessItem.PID = ProcessEntry.th32ProcessID;
		ProcessItem.Name = ProcessEntry.szExeFile;

		HANDLE ModuleSnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ProcessEntry.th32ProcessID);
		if (ModuleSnapshotHandle != INVALID_HANDLE_VALUE)
		{
			MODULEENTRY32 ModuleEntry;
			ModuleEntry.dwSize = sizeof(MODULEENTRY32);

			if (Module32First(ModuleSnapshotHandle, &ModuleEntry))
			{
				ProcessItem.Path = ModuleEntry.szExePath; // Store the full Path of the executable
			}

			CloseHandle(ModuleSnapshotHandle);
		}

		if (IsSameBitsProcess(ProcessItem.Path))
		{
			ProcessList.push_back(ProcessItem);
		}
	} while (Process32Next(ProcessSnapshotHandle, &ProcessEntry));

	CloseHandle(ProcessSnapshotHandle);

	return ProcessList;
}

std::vector<FProcessListItem> GetProcessList(const std::string& Filter)
{
	std::vector<FProcessListItem> List;

	HANDLE ProcessSnapshotHandle;
	PROCESSENTRY32 ProcessEntry;

	ProcessSnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (ProcessSnapshotHandle == INVALID_HANDLE_VALUE)
	{
		return List;
	}

	ProcessEntry.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(ProcessSnapshotHandle, &ProcessEntry))
	{
		CloseHandle(ProcessSnapshotHandle);
		return List;
	}

	do
	{
		FProcessListItem ProcessItem;
		ProcessItem.PID = ProcessEntry.th32ProcessID;
		ProcessItem.Name = ProcessEntry.szExeFile;
		if (ProcessItem.Name.find(Filter) != std::string::npos)
		{
			HANDLE ModuleSnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, ProcessEntry.th32ProcessID);
			if (ModuleSnapshotHandle != INVALID_HANDLE_VALUE)
			{
				MODULEENTRY32 ModuleEntry;
				ModuleEntry.dwSize = sizeof(MODULEENTRY32);

				if (Module32First(ModuleSnapshotHandle, &ModuleEntry))
				{
					ProcessItem.Path = ModuleEntry.szExePath; // Store the full Path of the executable
				}

				CloseHandle(ModuleSnapshotHandle);
			}

			if (IsSameBitsProcess(ProcessItem.Path))
			{
				List.push_back(ProcessItem);
			}
		}
	} while (Process32Next(ProcessSnapshotHandle, &ProcessEntry));

	CloseHandle(ProcessSnapshotHandle);
	return List;
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

FProcess::FProcess(DWORD PID)
{
	this->PID = PID;
	ProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
	if (ProcessHandle == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::Log("Failed to open process");
	}
	CheckBits();

	char szProcessName[MAX_PATH] = "<unknown>";
	GetModuleBaseNameA(ProcessHandle, NULL, szProcessName, sizeof(szProcessName) / sizeof(char));
	this->ProcessName = szProcessName;
}

FProcess::FProcess(const std::string& ProcessName)
{
	this->PID = GetProcessID(ProcessName);
	ProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
	if (ProcessHandle == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::Log("Failed to open process");
	}
	CheckBits();
	char szProcessName[MAX_PATH] = "<unknown>";
	GetModuleBaseNameA(ProcessHandle, NULL, szProcessName, sizeof(szProcessName) / sizeof(char));
	this->ProcessName = szProcessName;
}

DWORD FProcess::GetProcessID(const std::string& ProcessName)
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

bool FProcess::IsValid()
{
	return this->ProcessHandle != INVALID_HANDLE_VALUE;
}

bool FProcess::Is32Bit()
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
		fnIsWow64Process(this->ProcessHandle, &bIsWow64);
		return bIsWow64;
	}
	ClassDumper3::Log("Error function IsWow64Process does not exist");
	return false;
}

bool FProcess::IsSameBits()
{
	bool b64Remote = !Is32Bit();
	bool b64Local = sizeof(void*) == 8;
	return b64Remote == b64Local;
}

bool FProcess::AttachDebugger()
{
	return DebugActiveProcess(PID);
}

bool FProcess::DetachDebugger()
{
	return DebugActiveProcessStop(PID);
}

bool FProcess::DebugWait()
{
	return WaitForDebugEvent(&DebugEvent, INFINITE);
}

bool FProcess::DebugContinue(DWORD ContinueStatus)
{
	return ContinueDebugEvent(DebugEvent.dwProcessId, DebugEvent.dwThreadId, ContinueStatus);
}

void* FProcess::AllocRW(size_t size)
{
	return VirtualAllocEx(ProcessHandle, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void* FProcess::AllocRWX(size_t size)
{
	return VirtualAllocEx(ProcessHandle, NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

bool FProcess::Free(void* Address)
{
	return VirtualFreeEx(ProcessHandle, Address, 0, MEM_RELEASE);
}

void FProcess::CheckBits()
{
	if (!IsSameBits())
	{
		ClassDumper3::Log("Process is not the same bits");
	}
}


FProcess& FProcess::operator=(const FProcess& Other)
{
	ProcessHandle = Other.ProcessHandle;
	PID = Other.PID;
	ProcessName = Other.ProcessName;
	return *this;
}

FProcess::FProcess(const FProcess& Other)
{
	ProcessHandle = Other.ProcessHandle;
	PID = Other.PID;
	ProcessName = Other.ProcessName;
}

FProcess::FProcess(HANDLE InProcessHandle, DWORD InPID, const std::string& InProcessName)
{
	ProcessHandle = InProcessHandle;
	PID = InPID;
	ProcessName = InProcessName;
}

FProcess::FProcess()
{
	ProcessHandle = INVALID_HANDLE_VALUE;
	PID = NULL;
	ProcessName = "";
}

FMemoryRange::FMemoryRange(uintptr_t InStart, uintptr_t InEnd, bool InbExecutable, bool InbReadable, bool InbWritable)
{
	Start = InStart;
	End = InEnd;
	bExecutable = InbExecutable;
	bReadable = InbReadable;
	bWritable = InbWritable;
}

bool FMemoryRange::Contains(uintptr_t Address) const
{
	return Address >= Start && Address <= End;
}

uintptr_t FMemoryRange::Size() const
{
	return End - Start;
}

void FMemoryMap::Setup(FProcess* process)
{
	// Get readable memory ranges
	MEMORY_BASIC_INFORMATION MBI;
	for (uintptr_t Address = 0; VirtualQueryEx(process->ProcessHandle, (LPCVOID)Address, &MBI, sizeof(MBI)); Address += MBI.RegionSize)
	{
		if (MBI.State == MEM_COMMIT && (MBI.Protect & PAGE_READWRITE || MBI.Protect & PAGE_EXECUTE_READWRITE))
		{
			Ranges.push_back(
				FMemoryRange(
					(uintptr_t)MBI.BaseAddress,
					(uintptr_t)MBI.BaseAddress + MBI.RegionSize,
					MBI.Protect & PAGE_EXECUTE,
					MBI.Protect & PAGE_READONLY,
					MBI.Protect & PAGE_READWRITE));
		}
	}
	if (Ranges.size() == 0)
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

void FModuleMap::Setup(FProcess* process)
{
	MODULEENTRY32 me32;
	me32.dwSize = sizeof(MODULEENTRY32);
	HANDLE hModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process->PID);
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
		Modules.push_back(FModule());
		FModule& Module = Modules.back();
		Module.baseAddress = me32.modBaseAddr;
		Module.name = me32.szModule;
		IMAGE_DOS_HEADER dosHeader;
		ReadProcessMemory(process->ProcessHandle, me32.modBaseAddr, &dosHeader, sizeof(dosHeader), nullptr);
		IMAGE_NT_HEADERS ntHeaders;
		ReadProcessMemory(process->ProcessHandle, (void*)((uintptr_t)me32.modBaseAddr + dosHeader.e_lfanew), &ntHeaders, sizeof(ntHeaders), nullptr);
		IMAGE_SECTION_HEADER* sectionHeaders = new IMAGE_SECTION_HEADER[ntHeaders.FileHeader.NumberOfSections];
		ReadProcessMemory(process->ProcessHandle, (void*)((uintptr_t)me32.modBaseAddr + dosHeader.e_lfanew + sizeof(IMAGE_NT_HEADERS)), sectionHeaders, sizeof(IMAGE_SECTION_HEADER) * ntHeaders.FileHeader.NumberOfSections, nullptr);
		for (int i = 0; i < ntHeaders.FileHeader.NumberOfSections; i++)
		{
			IMAGE_SECTION_HEADER& sectionHeader = sectionHeaders[i];
			Module.sections.push_back(ModuleSection());
			ModuleSection& section = Module.sections.back();
			section.name = (char*)sectionHeader.Name;
			section.start = (uintptr_t)me32.modBaseAddr + sectionHeader.VirtualAddress;
			section.end = section.start + sectionHeader.Misc.VirtualSize;
			section.bFlagExecutable = sectionHeader.Characteristics & IMAGE_SCN_MEM_EXECUTE;
			section.bFlagReadonly = sectionHeader.Characteristics & IMAGE_SCN_MEM_READ;
		}
	} while (Module32Next(hModuleSnap, &me32));
	CloseHandle(hModuleSnap);

	ClassDumper3::LogF("Found %d modules", Modules.size());
}

FModule* FModuleMap::GetModule(const char* name)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [name](const FModule& module) { return module.name == name; });
	if (it != Modules.end()) return &*it;
	return nullptr;
}

FModule* FModuleMap::GetModule(const std::string& name)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [name](const FModule& module) { return module.name == name; });
	if (it != Modules.end()) return &*it;
	return nullptr;
}

FModule* FModuleMap::GetModule(uintptr_t address)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [address](const FModule& module)
		{
			return address >= (uintptr_t)module.baseAddress && address <= (uintptr_t)module.baseAddress + module.sections.back().end;
		});
	if (it != Modules.end()) return &*it;
	return nullptr;
}

void FTargetProcess::Setup(const std::string& processName)
{
	process = FProcess(processName);
	memoryMap.Setup(&process);
	moduleMap.Setup(&process);
}

void FTargetProcess::Setup(DWORD processID)
{
	process = FProcess(processID);
	memoryMap.Setup(&process);
	moduleMap.Setup(&process);
}

void FTargetProcess::Setup(FProcess process)
{
	this->process = process;
	memoryMap.Setup(&process);
	moduleMap.Setup(&process);
}

bool FTargetProcess::Is64Bit()
{
	return !process.Is32Bit();
}

bool FTargetProcess::IsValid()
{
	return process.IsValid();
}

FModule* FTargetProcess::GetModule(const std::string& moduleName)
{
	for (auto& module : moduleMap.Modules)
	{
		if (module.name.find(moduleName) != std::string::npos)
		{
			return &module;
		}
	}
	return nullptr;
};

FMemoryRange* FTargetProcess::GetMemoryRange(uintptr_t address)
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

std::vector<MemoryBlock> FTargetProcess::GetReadableMemory()
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
					block.blockAddress = (void*)range.Start;
					block.blockSize = range.Size();
					block.blockCopy = malloc(block.blockSize);
					ReadProcessMemory(process.ProcessHandle, block.blockAddress, block.blockCopy, block.blockSize, NULL);
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

std::vector<std::future<MemoryBlock>> FTargetProcess::AsyncGetReadableMemory()
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
					block.blockAddress = (void*)range.Start;
					block.blockSize = range.Size();
					block.blockCopy = malloc(block.blockSize);
					ReadProcessMemory(process.ProcessHandle, block.blockAddress, block.blockCopy, block.blockSize, NULL);
					return block;
				});
			futures.push_back(std::move(future));
		}
	}
	return futures;
}

ModuleSection* FTargetProcess::GetModuleSection(uintptr_t address)
{
	for (auto& module : moduleMap.Modules)
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


DWORD FTargetProcess::SetProtection(uintptr_t address, size_t size, DWORD protection)
{
	DWORD oldProtection;
	VirtualProtectEx(process.ProcessHandle, (void*)address, size, protection, &oldProtection);
	return oldProtection;
}

std::vector<HWND> FTargetProcess::GetWindows()
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
		if (processID == process.PID)
		{
			windowsFinal.push_back(window);
		}
	}

	return windowsFinal;
}

std::string FTargetProcess::GetWindowName(HWND window)
{
	char buffer[256];
	GetWindowTextA(window, buffer, 256);
	return std::string(buffer);
}

bool FTargetProcess::SetWindowName(HWND window, const std::string& name)
{
	return SetWindowTextA(window, name.c_str());
}

bool FTargetProcess::SetTransparency(HWND window, BYTE alpha)
{
	return SetLayeredWindowAttributes(window, 0, alpha, LWA_ALPHA);
}

void FTargetProcess::Read(uintptr_t address, void* buffer, size_t size)
{
	if (!ReadProcessMemory(process.ProcessHandle, (void*)address, buffer, size, NULL))
	{
		printf("ReadProcessMemory failed: %d\n", GetLastError());
	}
}

std::future<void*> FTargetProcess::AsyncRead(uintptr_t address, size_t size)
{
	return std::async(std::launch::async, [this, address, size]()
		{
			void* buffer = malloc(size);
			ReadProcessMemory(process.ProcessHandle, (void*)address, buffer, size, NULL);
			return buffer;
		});
}

void FTargetProcess::Write(uintptr_t address, void* buffer, size_t size)
{
	WriteProcessMemory(process.ProcessHandle, (void*)address, buffer, size, NULL);
}

void FTargetProcess::AsyncWrite(uintptr_t address, void* buffer, size_t size)
{
	auto result = std::async(std::launch::async, [this, address, buffer, size]()
		{
			WriteProcessMemory(process.ProcessHandle, (void*)address, buffer, size, NULL);
		});
}

HANDLE FTargetProcess::InjectDLL(const std::string& dllPath)
{
	HANDLE hThread = NULL;
	LPVOID LoadLibraryAAddr = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
	LPVOID RemoteString = (LPVOID)VirtualAllocEx(process.ProcessHandle, NULL, dllPath.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	WriteProcessMemory(process.ProcessHandle, (LPVOID)RemoteString, dllPath.c_str(), dllPath.size(), NULL);
	hThread = CreateRemoteThread(process.ProcessHandle, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibraryAAddr, (LPVOID)RemoteString, NULL, NULL);
	return hThread;
}

void FTargetProcess::InjectDLLAsync(const std::string& dllPath)
{
	auto result = std::async(std::launch::async, [this, dllPath]()
		{
			InjectDLL(dllPath);
		});
}