#include "Memory.h"
#include "../ClassDumper3.h"

bool ShouldIgnoreProcess(const std::string& ProcessName)
{
	static const std::vector<std::string> IgnoreList
	{
		"[System Process]",
		"svchost.exe",
		"explorer.exe",
		"conhost.exe"
	};

	for (const std::string& Name : IgnoreList)
	{
		if (ProcessName == Name)
		{
			return true;
		}
	}

	return false;
}

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

		if (IsSameBitsProcess(ProcessItem.Path) && !ShouldIgnoreProcess(ProcessItem.Name))
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

			if (IsSameBitsProcess(ProcessItem.Path) && !ShouldIgnoreProcess(ProcessItem.Name))
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

FProcess::FProcess(DWORD InPID)
{
	PID = InPID;
	ProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
	if (ProcessHandle != INVALID_HANDLE_VALUE)
	{
		char szProcessName[MAX_PATH] = "<unknown>";
		GetModuleBaseNameA(ProcessHandle, NULL, szProcessName, sizeof(szProcessName) / sizeof(char));

		ProcessName = szProcessName;
	}
	else
	{
		ClassDumper3::Log("Failed to open process");
		return;
	}

}

FProcess::FProcess(const std::string& ProcessName)
{
	this->PID = GetProcessID(ProcessName);
	ProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
	if (ProcessHandle == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::Log("Failed to open process");
		return;
	}
	char szProcessName[MAX_PATH] = "<unknown>";
	GetModuleBaseNameA(ProcessHandle, NULL, szProcessName, sizeof(szProcessName) / sizeof(char));
	this->ProcessName = szProcessName;
}

DWORD FProcess::GetProcessID(const std::string& ProcessName)
{
	PROCESSENTRY32 ProcessEntry;
	ProcessEntry.dwSize = sizeof(PROCESSENTRY32);
	
	HANDLE ProcessSnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (ProcessSnapshotHandle == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::Log("Failed to create snapshot");

		return 0;
	}
	if (!Process32First(ProcessSnapshotHandle, &ProcessEntry))
	{
		ClassDumper3::Log("Failed to get first process");
		CloseHandle(ProcessSnapshotHandle);
		return 0;
	}
	do
	{
		if (ProcessName == ProcessEntry.szExeFile)
		{
			CloseHandle(ProcessSnapshotHandle);
			return ProcessEntry.th32ProcessID;
		}
	} while (Process32Next(ProcessSnapshotHandle, &ProcessEntry));
	
	CloseHandle(ProcessSnapshotHandle);
	return 0;
}

bool FProcess::IsValid()
{
	return ProcessHandle != INVALID_HANDLE_VALUE;
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

FProcess::FProcess(const FProcess& Other) :
	ProcessHandle(Other.ProcessHandle),
	PID(Other.PID),
	ProcessName(Other.ProcessName)
{
}

FProcess::FProcess(HANDLE InProcessHandle, DWORD InPID, const std::string& InProcessName) :
	ProcessHandle(InProcessHandle),
	PID(InPID), 
	ProcessName(InProcessName)
{
}

FProcess::FProcess() :
	ProcessHandle(INVALID_HANDLE_VALUE),
	PID(0),
	ProcessName("")
{
}

FMemoryRange::FMemoryRange(uintptr_t InStart, uintptr_t InEnd, bool InbExecutable, bool InbReadable, bool InbWritable) : 
	Start(InStart),
	End(InEnd),
	bExecutable(InbExecutable),
	bReadable(InbReadable),
	bWritable(InbWritable)
{
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
	const DWORD ExecuteFlags = (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
	const DWORD ReadFlags = (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY);
	const DWORD WriteFlags = (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_READWRITE | PAGE_WRITECOPY);
	
	// Get readable memory ranges
	MEMORY_BASIC_INFORMATION MBI;
	for (uintptr_t Address = 0; VirtualQueryEx(process->ProcessHandle, (LPCVOID)Address, &MBI, sizeof(MBI)); Address += MBI.RegionSize)
	{
		if (MBI.State == MEM_COMMIT && (MBI.Protect & ExecuteFlags || MBI.Protect & ReadFlags || MBI.Protect & WriteFlags))
		{
			Ranges.push_back(
				FMemoryRange(
					(uintptr_t)MBI.BaseAddress,
					(uintptr_t)MBI.BaseAddress + MBI.RegionSize,
					(MBI.Protect & ExecuteFlags),
					MBI.Protect & ReadFlags,
					MBI.Protect & WriteFlags));
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

void FModuleMap::Setup(const FProcess* Process)
{
	MODULEENTRY32 ModuleEntry;
	ModuleEntry.dwSize = sizeof(MODULEENTRY32);
	HANDLE ModuleSnapshotHandle = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, Process->PID);
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
		
		IMAGE_DOS_HEADER DOSHeader;
		IMAGE_NT_HEADERS NTHeaders;
		IMAGE_SECTION_HEADER* SectionHeaders = nullptr;
		
		ReadProcessMemory(Process->ProcessHandle, ModuleEntry.modBaseAddr, &DOSHeader, sizeof(DOSHeader), nullptr);
		
		ReadProcessMemory(Process->ProcessHandle, (void*)((uintptr_t)ModuleEntry.modBaseAddr + DOSHeader.e_lfanew), &NTHeaders, sizeof(NTHeaders), nullptr);
		
		SectionHeaders = new IMAGE_SECTION_HEADER[NTHeaders.FileHeader.NumberOfSections];
		
		ReadProcessMemory(Process->ProcessHandle, (void*)((uintptr_t)ModuleEntry.modBaseAddr + DOSHeader.e_lfanew + sizeof(IMAGE_NT_HEADERS)), SectionHeaders, sizeof(IMAGE_SECTION_HEADER) * NTHeaders.FileHeader.NumberOfSections, nullptr);
		for (int i = 0; i < NTHeaders.FileHeader.NumberOfSections; i++)
		{
			IMAGE_SECTION_HEADER& SectionHeader = SectionHeaders[i];
			
			Module.Sections.push_back(FModuleSection());
			FModuleSection& Section = Module.Sections.back();
			
			Section.Name = (char*)SectionHeader.Name;
			Section.Start = (uintptr_t)ModuleEntry.modBaseAddr + SectionHeader.VirtualAddress;
			Section.End = Section.Start + SectionHeader.Misc.VirtualSize;
			Section.bFlagExecutable = SectionHeader.Characteristics & IMAGE_SCN_MEM_EXECUTE;
			Section.bFlagReadonly = SectionHeader.Characteristics & IMAGE_SCN_MEM_READ;
		}
	} while (Module32Next(ModuleSnapshotHandle, &ModuleEntry));
	CloseHandle(ModuleSnapshotHandle);

	ClassDumper3::LogF("Found %d modules", Modules.size());
}

FModule* FModuleMap::GetModule(const char* Name)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [Name](const FModule& Module) { return Module.Name == Name; });
	if (it != Modules.end()) return &*it;
	return nullptr;
}

FModule* FModuleMap::GetModule(const std::string& Name)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [Name](const FModule& Module) { return Module.Name == Name; });
	if (it != Modules.end()) return &*it;
	return nullptr;
}

FModule* FModuleMap::GetModule(const uintptr_t Address)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [Address](const FModule& Module)
		{
			return Address >= (uintptr_t)Module.BaseAddress && Address <= (uintptr_t)Module.BaseAddress + Module.Sections.back().End;
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
	return Process.IsValid() && ModuleMap.Modules.size();
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

FMemoryRange* FTargetProcess::GetMemoryRange(const uintptr_t Address)
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

std::vector<FMemoryBlock> FTargetProcess::GetReadableMemory()
{
	std::vector<std::future<FMemoryBlock>> Futures;
	std::vector<FMemoryBlock> Blocks;
	for (auto& Range : MemoryMap.Ranges)
	{
		if (Range.bReadable)
		{
			// get a future using async launch lambda
			auto Future = std::async(std::launch::async, [&Range, &Process = Process]()
				{
					FMemoryBlock Block;
					
					Block.Address = (void*)Range.Start;
					Block.Size = Range.Size();
					Block.Copy = malloc(Block.Size);
					
					if (!Block.Copy) return Block;
					
					ReadProcessMemory(Process.ProcessHandle, Block.Address, Block.Copy, Block.Size, NULL);
					return Block;
				});
			Futures.push_back(std::move(Future));
		}
	}

	// wait for all Futures to finish
	for (auto& Future : Futures)
	{
		Blocks.push_back(Future.get());
	}

	return Blocks;
}

std::vector<std::future<FMemoryBlock>> FTargetProcess::AsyncGetReadableMemory()
{
	std::vector<std::future<FMemoryBlock>> futures;

	for (auto& Range : MemoryMap.Ranges)
	{
		if (Range.bReadable)
		{
			auto future = std::async(std::launch::async, [&Range, &Process = Process]()
				{
					FMemoryBlock Block(Range.Start, Range.Size());

					if (!Block.Copy) return Block;

					ReadProcessMemory(Process.ProcessHandle, Block.Address, Block.Copy, Block.Size, NULL);
					return Block;
				});
			futures.push_back(std::move(future));
		}
		
	}

	return futures;
}

std::vector<std::future<FMemoryBlock>> FTargetProcess::AsyncGetExecutableMemory()
{
	std::vector<std::future<FMemoryBlock>> futures;

	for (auto& Range : MemoryMap.Ranges)
	{
		if (Range.bExecutable)
		{
			auto future = std::async(std::launch::async, [&Range, &Process = Process]()
				{
					FMemoryBlock Block(Range.Start, Range.Size());

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
	std::vector<HWND> TargetWindows;
	auto EnumLambda = [](HWND hwnd, LPARAM lParam) -> BOOL
	{
		auto& WindowList = *reinterpret_cast<std::vector<HWND>*>(lParam);;
		return TRUE;
	};
	
	DWORD TargetPID = Process.PID;
	auto EraseIfLambda = [TargetPID](HWND Window)
	{
		DWORD WindowPID = 0;
		GetWindowThreadProcessId(Window, &WindowPID);
		return TargetPID != WindowPID;
	};
	
	// Get all windows and filter out our targets
	EnumWindows(EnumLambda, reinterpret_cast<LPARAM>(&TargetWindows));
	TargetWindows.erase(std::remove_if(TargetWindows.begin(), TargetWindows.end(), EraseIfLambda), TargetWindows.end());

	return TargetWindows;
}

std::string FTargetProcess::GetWindowName(HWND Window)
{
	char buffer[256];
	GetWindowTextA(Window, buffer, 256);
	return std::string(buffer);
}

bool FTargetProcess::SetWindowName(HWND Window, const std::string& Name)
{
	return SetWindowTextA(Window, Name.c_str());
}

bool FTargetProcess::SetTransparency(HWND Window, BYTE Alpha)
{
	return SetLayeredWindowAttributes(Window, 0, Alpha, LWA_ALPHA);
}

void FTargetProcess::Read(uintptr_t Address, void* Buffer, size_t Size)
{
	if (!ReadProcessMemory(Process.ProcessHandle, (void*)Address, Buffer, Size, NULL))
	{
		printf("ReadProcessMemory failed: %d\n", GetLastError());
	}
}

std::future<void*> FTargetProcess::AsyncRead(uintptr_t Address, size_t Size)
{
	return std::async(std::launch::async, [this, Address, Size]()
		{
			void* Buffer = malloc(Size);
			if (Buffer)
			{
				ReadProcessMemory(Process.ProcessHandle, (void*)Address, Buffer, Size, NULL);
			}
			return Buffer;
		});
}

void FTargetProcess::Write(uintptr_t Address, void* Buffer, size_t Size)
{
	WriteProcessMemory(Process.ProcessHandle, (void*)Address, Buffer, Size, NULL);
}

void FTargetProcess::AsyncWrite(uintptr_t Address, void* Buffer, size_t Size)
{
	auto result = std::async(std::launch::async, [this, Address, Buffer, Size]()
		{
			WriteProcessMemory(Process.ProcessHandle, (void*)Address, Buffer, Size, NULL);
		});
}

HANDLE FTargetProcess::InjectDLL(const std::string& DllPath)
{
	HANDLE DllInjectThreadHandle = NULL;
	
	HMODULE Kernel32 = GetModuleHandleA("kernel32.dll");
	if (!Kernel32) return DllInjectThreadHandle;
	
	LPVOID LoadLibraryAAddr = (LPVOID)GetProcAddress(Kernel32, "LoadLibraryA");
	if (!LoadLibraryAAddr) return DllInjectThreadHandle;
	
	LPVOID RemoteString = (LPVOID)VirtualAllocEx(Process.ProcessHandle, NULL, DllPath.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!RemoteString) return DllInjectThreadHandle;
	
	WriteProcessMemory(Process.ProcessHandle, (LPVOID)RemoteString, DllPath.c_str(), DllPath.size(), NULL);
	
	DllInjectThreadHandle = CreateRemoteThread(Process.ProcessHandle, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibraryAAddr, (LPVOID)RemoteString, NULL, NULL);
	return DllInjectThreadHandle;
}

std::future<HANDLE> FTargetProcess::InjectDLLAsync(const std::string& DllPath)
{
	return std::async(std::launch::async, [this, DllPath]()
		{
			return InjectDLL(DllPath);
		});
}