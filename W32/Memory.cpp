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

bool Is32BitExecutable(const std::string& FilePath, bool& bFailed)
{
	HANDLE FileHandle = CreateFileA(FilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (FileHandle == INVALID_HANDLE_VALUE)
	{
		bFailed = true;
		return false;
	}

	HANDLE FileMappingHandle = CreateFileMapping(FileHandle, NULL, PAGE_READONLY, 0, 0, NULL);
	if (!FileMappingHandle)
	{
		CloseHandle(FileHandle);
		bFailed = true;
		return false;
	}

	LPVOID pFileBase = MapViewOfFile(FileMappingHandle, FILE_MAP_READ, 0, 0, 0);
	if (!pFileBase)
	{
		bFailed = true;
		CloseHandle(FileMappingHandle);
		CloseHandle(FileHandle);
		return false;
	}

	IMAGE_DOS_HEADER* pDosHeader = static_cast<IMAGE_DOS_HEADER*>(pFileBase);
	if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE)
	{
		bFailed = true;
		UnmapViewOfFile(pFileBase);
		CloseHandle(FileMappingHandle);
		CloseHandle(FileHandle);
		return false;
	}

	IMAGE_NT_HEADERS* pNtHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(
		reinterpret_cast<char*>(pFileBase) + pDosHeader->e_lfanew);

	bool bIs64Bit = pNtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;

	UnmapViewOfFile(pFileBase);
	CloseHandle(FileMappingHandle);
	CloseHandle(FileHandle);

	bFailed = false;
	return !bIs64Bit;
}

bool IsSameBitsProcess(const std::string& FilePath)
{
	bool b32Local = !IsRunning64Bits();
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
	
	ClassDumper3::LogF("Found %u memory regions", Ranges.size());
}

bool FModuleSection::Contains(uintptr_t Address) const
{
	return Address >= Start && Address <= End;
}

uintptr_t FModuleSection::Size() const
{
	return End - Start;
}

void FModuleMap::Setup(FProcess* process)
{
	MODULEENTRY32 ModuleEntry;
	ModuleEntry.dwSize = sizeof(MODULEENTRY32);
	HANDLE ModuleSnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process->PID);
	if (ModuleSnapshotHandle == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::Log("Failed to create snapshot");
		return;
	}
	if (!Module32First(ModuleSnapshotHandle, &ModuleEntry))
	{
		ClassDumper3::Log("Failed to get first module");
		CloseHandle(ModuleSnapshotHandle);
		return;
	}
	do
	{
		Modules.push_back(FModule());
		FModule& Module = Modules.back();
		Module.BaseAddress = ModuleEntry.modBaseAddr;
		Module.Name = ModuleEntry.szModule;
		IMAGE_DOS_HEADER dosHeader;
		ReadProcessMemory(process->ProcessHandle, ModuleEntry.modBaseAddr, &dosHeader, sizeof(dosHeader), nullptr);
		IMAGE_NT_HEADERS ntHeaders;
		ReadProcessMemory(process->ProcessHandle, (void*)((uintptr_t)ModuleEntry.modBaseAddr + dosHeader.e_lfanew), &ntHeaders, sizeof(ntHeaders), nullptr);
		IMAGE_SECTION_HEADER* sectionHeaders = new IMAGE_SECTION_HEADER[ntHeaders.FileHeader.NumberOfSections];
		ReadProcessMemory(process->ProcessHandle, (void*)((uintptr_t)ModuleEntry.modBaseAddr + dosHeader.e_lfanew + sizeof(IMAGE_NT_HEADERS)), sectionHeaders, sizeof(IMAGE_SECTION_HEADER) * ntHeaders.FileHeader.NumberOfSections, nullptr);
		for (int i = 0; i < ntHeaders.FileHeader.NumberOfSections; i++)
		{
			IMAGE_SECTION_HEADER& sectionHeader = sectionHeaders[i];
			Module.Sections.push_back(FModuleSection());
			FModuleSection& section = Module.Sections.back();
			section.Name = (char*)sectionHeader.Name;
			section.Start = (uintptr_t)ModuleEntry.modBaseAddr + sectionHeader.VirtualAddress;
			section.End = section.Start + sectionHeader.Misc.VirtualSize;
			section.bFlagExecutable = sectionHeader.Characteristics & IMAGE_SCN_MEM_EXECUTE;
			section.bFlagReadonly = sectionHeader.Characteristics & IMAGE_SCN_MEM_READ;
		}
	} while (Module32Next(ModuleSnapshotHandle, &ModuleEntry));
	CloseHandle(ModuleSnapshotHandle);

	ClassDumper3::LogF("Found %d modules", Modules.size());
}

FModule* FModuleMap::GetModule(const char* name)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [name](const FModule& module) { return module.Name == name; });
	if (it != Modules.end()) return &*it;
	return nullptr;
}

FModule* FModuleMap::GetModule(const std::string& name)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [name](const FModule& module) { return module.Name == name; });
	if (it != Modules.end()) return &*it;
	return nullptr;
}

FModule* FModuleMap::GetModule(uintptr_t address)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [address](const FModule& module)
		{
			return address >= (uintptr_t)module.BaseAddress && address <= (uintptr_t)module.BaseAddress + module.Sections.back().End;
		});
	if (it != Modules.end()) return &*it;
	return nullptr;
}

void FTargetProcess::Setup(const std::string& InProcessName)
{
	Process = FProcess(InProcessName);
	MemoryMap.Setup(&Process);
	ModuleMap.Setup(&Process);
}

void FTargetProcess::Setup(DWORD InPID)
{
	Process = FProcess(InPID);
	MemoryMap.Setup(&Process);
	ModuleMap.Setup(&Process);
}

void FTargetProcess::Setup(const FProcess& InProcess)
{
	Process = InProcess;
	MemoryMap.Setup(&Process);
	ModuleMap.Setup(&Process);
}

bool FTargetProcess::IsValid()
{
	return Process.IsValid();
}

FModule* FTargetProcess::GetModule(const std::string& moduleName)
{
	for (auto& Module : ModuleMap.Modules)
	{
		if (Module.Name.find(moduleName) != std::string::npos)
		{
			return &Module;
		}
	}
	return nullptr;
};

FMemoryRange* FTargetProcess::GetMemoryRange(uintptr_t Address)
{
	for (auto& Range : MemoryMap.Ranges)
	{
		if (Range.Contains(Address))
		{
			return &Range;
		}
	}

	return nullptr;
}

std::vector<MemoryBlock> FTargetProcess::GetReadableMemory()
{
	std::vector<std::future<MemoryBlock>> Futures;
	std::vector<MemoryBlock> Blocks;
	for (auto& Range : MemoryMap.Ranges)
	{
		if (Range.bReadable)
		{
			// get a future using async launch lambda
			auto future = std::async(std::launch::async, [&Range, &Process = Process]()
				{
					MemoryBlock Block;
					
					Block.Address = (void*)Range.Start;
					Block.Size = Range.Size();
					Block.Copy = malloc(Block.Size);
					
					if (!Block.Copy) return Block;
					
					ReadProcessMemory(Process.ProcessHandle, Block.Address, Block.Copy, Block.Size, NULL);
					return Block;
				});
			Futures.push_back(std::move(future));
		}
	}

	// wait for all Futures to finish
	for (auto& Future : Futures)
	{
		Blocks.push_back(Future.get());
	}

	return Blocks;
}

std::vector<std::future<MemoryBlock>> FTargetProcess::AsyncGetReadableMemory()
{
	std::vector<std::future<MemoryBlock>> futures;
	for (auto& Range : MemoryMap.Ranges)
	{
		if (Range.bReadable)
		{
			// get a future using async launch lambda
			auto future = std::async(std::launch::async, [&Range, &Process = Process]()
				{
					MemoryBlock Block;
					
					Block.Address = (void*)Range.Start;
					Block.Size = Range.Size();
					Block.Copy = malloc(Block.Size);
					
					if (!Block.Copy) return Block;
					
					ReadProcessMemory(Process.ProcessHandle, Block.Address, Block.Copy, Block.Size, NULL);
					return Block;
				});
			futures.push_back(std::move(future));
		}
	}
	return futures;
}

FModuleSection* FTargetProcess::GetModuleSection(uintptr_t address)
{
	for (auto& Module : ModuleMap.Modules)
	{
		for (auto& Section : Module.Sections)
		{
			if (Section.Contains(address))
			{
				return &Section;
			}
		}
	}
	return nullptr;
}


DWORD FTargetProcess::SetProtection(uintptr_t address, size_t size, DWORD protection)
{
	DWORD oldProtection;
	VirtualProtectEx(Process.ProcessHandle, (void*)address, size, protection, &oldProtection);
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
		if (processID == Process.PID)
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
	if (!ReadProcessMemory(Process.ProcessHandle, (void*)address, buffer, size, NULL))
	{
		printf("ReadProcessMemory failed: %d\n", GetLastError());
	}
}

std::future<void*> FTargetProcess::AsyncRead(uintptr_t address, size_t size)
{
	return std::async(std::launch::async, [this, address, size]()
		{
			void* buffer = malloc(size);
			ReadProcessMemory(Process.ProcessHandle, (void*)address, buffer, size, NULL);
			return buffer;
		});
}

void FTargetProcess::Write(uintptr_t address, void* buffer, size_t size)
{
	WriteProcessMemory(Process.ProcessHandle, (void*)address, buffer, size, NULL);
}

void FTargetProcess::AsyncWrite(uintptr_t address, void* buffer, size_t size)
{
	auto result = std::async(std::launch::async, [this, address, buffer, size]()
		{
			WriteProcessMemory(Process.ProcessHandle, (void*)address, buffer, size, NULL);
		});
}

HANDLE FTargetProcess::InjectDLL(const std::string& dllPath)
{
	HANDLE hThread = NULL;
	LPVOID LoadLibraryAAddr = (LPVOID)GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
	LPVOID RemoteString = (LPVOID)VirtualAllocEx(Process.ProcessHandle, NULL, dllPath.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	WriteProcessMemory(Process.ProcessHandle, (LPVOID)RemoteString, dllPath.c_str(), dllPath.size(), NULL);
	hThread = CreateRemoteThread(Process.ProcessHandle, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibraryAAddr, (LPVOID)RemoteString, NULL, NULL);
	return hThread;
}

void FTargetProcess::InjectDLLAsync(const std::string& dllPath)
{
	auto result = std::async(std::launch::async, [this, dllPath]()
		{
			InjectDLL(dllPath);
		});
}