#include "Memory.h"
#include "../ClassDumper3.h"

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dbghelp.lib")

#include <psapi.h>
#include <winternl.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <thread>

// ---------------------------------------------
// Process Listing / Utility
// ---------------------------------------------

bool IgnoreProcess(const std::string& ProcessName)
{
	static const std::vector<std::string> IgnoreList{
		"[System Process]", "svchost.exe", "explorer.exe", "conhost.exe"
	};

	return std::any_of(
		IgnoreList.begin(), IgnoreList.end(),
		[&](const std::string& Name) { return ProcessName == Name; }
	);
}

std::vector<FProcessListItem> GetProcessList(const std::string& Filter)
{
	std::vector<FProcessListItem> List;
	const bool bUseFilter = !Filter.empty();

	HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (Snapshot == INVALID_HANDLE_VALUE)
	{
		return List;
	}

	PROCESSENTRY32 Entry = { sizeof(PROCESSENTRY32) };
	if (!Process32First(Snapshot, &Entry))
	{
		CloseHandle(Snapshot);
		return List;
	}

	do
	{
		FProcessListItem Item;
		Item.PID = Entry.th32ProcessID;
		Item.Name = Entry.szExeFile;
		Item.ProcessListName = std::to_string(Item.PID) + " : " + Item.Name;

		if (bUseFilter && Item.Name.find(Filter) == std::string::npos)
		{
			continue;
		}

		HANDLE ModuleSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, Entry.th32ProcessID);
		if (ModuleSnap == INVALID_HANDLE_VALUE)
		{
			continue;
		}

		MODULEENTRY32 ModuleEntry = { sizeof(MODULEENTRY32) };
		if (Module32First(ModuleSnap, &ModuleEntry))
		{
			Item.Path = ModuleEntry.szExePath;
		}

		CloseHandle(ModuleSnap);

		if (Item.Path.empty() || !IsSameBitsProcess(Item.Path) || IgnoreProcess(Item.Name))
		{
			continue;
		}

		List.push_back(std::move(Item));
	} while (Process32Next(Snapshot, &Entry));

	CloseHandle(Snapshot);
	return List;
}

bool GetDebugPrivilege()
{
	using RtlAdjustPrivilegeFn = NTSTATUS(__stdcall*)(DWORD, BOOL, INT, PBOOL);
	BOOL bWasEnabled = 0;

	HMODULE NTDLL = LoadLibraryA("ntdll.dll");
	if (!NTDLL)
	{
		return bWasEnabled;
	}

	auto RtlAdjustPrivilege = (RtlAdjustPrivilegeFn)GetProcAddress(NTDLL, "RtlAdjustPrivilege");
	if (!RtlAdjustPrivilege)
	{
		return bWasEnabled;
	}

	RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, TRUE, FALSE, &bWasEnabled);
	return bWasEnabled;
}

bool Is32BitExecutable(const std::string& FilePath, bool& bFailed)
{
	HANDLE File = CreateFileA(FilePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (File == INVALID_HANDLE_VALUE)
	{
		bFailed = true;
		return false;
	}

	HANDLE Mapping = CreateFileMapping(File, NULL, PAGE_READONLY, 0, 0, NULL);
	if (!Mapping)
	{
		CloseHandle(File);
		bFailed = true;
		return false;
	}

	void* Base = MapViewOfFile(Mapping, FILE_MAP_READ, 0, 0, 0);
	if (!Base)
	{
		CloseHandle(Mapping);
		CloseHandle(File);
		bFailed = true;
		return false;
	}

	const auto* DosHeader = static_cast<IMAGE_DOS_HEADER*>(Base);
	if (DosHeader->e_magic != IMAGE_DOS_SIGNATURE)
	{
		UnmapViewOfFile(Base);
		CloseHandle(Mapping);
		CloseHandle(File);
		bFailed = true;
		return false;
	}

	auto* NtHeaders = reinterpret_cast<IMAGE_NT_HEADERS*>(reinterpret_cast<char*>(Base) + DosHeader->e_lfanew);
	bool bIs64Bit = NtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC;

	UnmapViewOfFile(Base);
	CloseHandle(Mapping);
	CloseHandle(File);

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

FProcess::FProcess(DWORD InPID) : PID(InPID)
{
	ProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
	if (ProcessHandle != INVALID_HANDLE_VALUE)
	{
		char szProcessName[MAX_PATH] = "<unknown>";
		GetModuleBaseNameA(ProcessHandle, NULL, szProcessName, sizeof(szProcessName) / sizeof(char));
		ProcessName = szProcessName;
	}
	else
	{
		ClassDumper3::LogF("Failed to open process - error code: %u", GetLastError());
		return;
	}
}

FProcess::FProcess(const std::string& InProcessName)
{
	PID = GetProcessID(InProcessName);
	ProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
	if (ProcessHandle == INVALID_HANDLE_VALUE)
	{
		ClassDumper3::LogF("Failed to open process %s", InProcessName.c_str());
		return;
	}
	char szProcessName[MAX_PATH] = "<unknown>";
	GetModuleBaseNameA(ProcessHandle, NULL, szProcessName, sizeof(szProcessName) / sizeof(char));
	ProcessName = szProcessName;
}

FProcess::FProcess(HANDLE InProcessHandle, DWORD InPID, const std::string& InProcessName)
	: ProcessHandle(InProcessHandle), PID(InPID), ProcessName(InProcessName)
{
}

FProcess& FProcess::operator=(const FProcess& Other)
{
	ProcessHandle = Other.ProcessHandle;
	PID = Other.PID;
	ProcessName = Other.ProcessName;
	return *this;
}

FProcess::FProcess(const FProcess& Other) : ProcessHandle(Other.ProcessHandle), PID(Other.PID), ProcessName(Other.ProcessName) {}

FProcess::FProcess() : ProcessHandle(INVALID_HANDLE_VALUE), PID(0), ProcessName("") {}

DWORD FProcess::GetProcessID(const std::string& Name)
{
	HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (Snapshot == INVALID_HANDLE_VALUE)
		return 0;

	PROCESSENTRY32 Entry = {sizeof(PROCESSENTRY32)};
	if (!Process32First(Snapshot, &Entry))
	{
		CloseHandle(Snapshot);
		return 0;
	}

	do
	{
		if (Name == Entry.szExeFile)
		{
			CloseHandle(Snapshot);
			return Entry.th32ProcessID;
		}
	} while (Process32Next(Snapshot, &Entry));

	CloseHandle(Snapshot);
	return 0;
}

void* FProcess::AllocRW(size_t Size)
{
	return VirtualAllocEx(ProcessHandle, nullptr, Size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void* FProcess::AllocRWX(size_t Size)
{
	return VirtualAllocEx(ProcessHandle, nullptr, Size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
}

bool FProcess::Free(void* Address)
{
	return VirtualFreeEx(ProcessHandle, Address, 0, MEM_RELEASE);
}

FMemoryRange::FMemoryRange(uintptr_t InStart, uintptr_t InEnd, bool InbExecutable, bool InbReadable, bool InbWritable)
	: Start(InStart), End(InEnd), bExecutable(InbExecutable), bReadable(InbReadable), bWritable(InbWritable)
{
}

FMemoryMap::FMemoryMap(const FProcess& Process)
{
	const DWORD ExecuteFlags = (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY);
	const DWORD ReadFlags = (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY);
	const DWORD WriteFlags = (PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY | PAGE_READWRITE | PAGE_WRITECOPY);

	// Get readable memory ranges
	MEMORY_BASIC_INFORMATION MBI;
	for (uintptr_t Address = 0; VirtualQueryEx(Process.ProcessHandle, (LPCVOID)Address, &MBI, sizeof(MBI)); Address += MBI.RegionSize)
	{
		if (MBI.State == MEM_COMMIT && (MBI.Protect & ExecuteFlags || MBI.Protect & ReadFlags || MBI.Protect & WriteFlags))
		{
			Ranges.push_back(FMemoryRange((uintptr_t)MBI.BaseAddress, (uintptr_t)MBI.BaseAddress + MBI.RegionSize, (MBI.Protect & ExecuteFlags),
										  MBI.Protect & ReadFlags, MBI.Protect & WriteFlags));
		}
	}
	if (Ranges.empty())
	{
		ClassDumper3::LogF("Failed to get memory ranges error code: %u", GetLastError());
	}

	ClassDumper3::LogF("Found %u memory regions", Ranges.size());
}

FMemoryBlock::FMemoryBlock(void* InAddress, size_t InSize) : Address(InAddress), Size(InSize), Copy(InSize) {}

FMemoryBlock::FMemoryBlock(uintptr_t InAddress, size_t InSize) : Address(reinterpret_cast<void*>(InAddress)), Size(InSize), Copy(InSize) {}

FModuleSection::FModuleSection(uintptr_t InStart, uintptr_t InEnd, bool InbFlagReadonly, bool InbFlagExecutable, const std::string& InName)
	: Start(InStart), End(InEnd), bFlagReadonly(InbFlagReadonly), bFlagExecutable(InbFlagExecutable), Name(InName)
{
}

FModuleSection::FModuleSection(const FModuleSection& Other)
	: Start(Other.Start), End(Other.End), bFlagReadonly(Other.bFlagReadonly), bFlagExecutable(Other.bFlagExecutable), Name(Other.Name)
{
}

bool FMemoryRange::Contains(uintptr_t Address) const
{
	return Address >= Start && Address <= End;
}

bool FModuleSection::Contains(uintptr_t Address) const
{
	return Address >= Start && Address <= End;
}

uintptr_t FMemoryRange::Size() const
{
	return End - Start;
}

uintptr_t FModuleSection::Size() const
{
	return End - Start;
}

FModule::FModule(void* InBaseAddress, const std::vector<FModuleSection>& InSections, const std::string& InName)
	: BaseAddress(InBaseAddress), Sections(InSections), Name(InName)
{
}

FModule::FModule(const FModule& Other) : BaseAddress(Other.BaseAddress), Sections(Other.Sections), Name(Other.Name) {}

FModuleMap::FModuleMap(const FProcess& Process)
{
	LoadModules(Process);
}

FModule* FModuleMap::GetModule(const char* Name)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [Name](const FModule& Module) { return Module.Name == Name; });
	if (it != Modules.end())
		return &*it;
	return nullptr;
}

FModule* FModuleMap::GetModule(const std::string& Name)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [Name](const FModule& Module) { return Module.Name == Name; });
	if (it != Modules.end())
		return &*it;
	return nullptr;
}

FModule* FModuleMap::GetModule(const uintptr_t Address)
{
	auto it = std::find_if(Modules.begin(), Modules.end(), [Address](const FModule& Module)
						   { return Address >= (uintptr_t)Module.BaseAddress && Address <= (uintptr_t)Module.BaseAddress + Module.Sections.back().End; });
	if (it != Modules.end())
		return &*it;
	return nullptr;
}

void FModuleMap::LoadModules(const FProcess& Process)
{
	HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, Process.PID);
	if (Snapshot == INVALID_HANDLE_VALUE)
		return;

	MODULEENTRY32 Entry = { sizeof(MODULEENTRY32) };
	if (!Module32First(Snapshot, &Entry)) {
		CloseHandle(Snapshot);
		return;
	}

	do {
		Modules.emplace_back(ParseModule(Process, Entry));
	} while (Module32Next(Snapshot, &Entry));

	CloseHandle(Snapshot);
}


FModule FModuleMap::ParseModule(const FProcess& Process, const MODULEENTRY32& Entry)
{
	FModule Module;
	Module.BaseAddress = Entry.modBaseAddr;
	Module.Name = Entry.szModule;
	Module.Sections = ParseSections(Process, Entry);
	return Module;
}

std::vector<FModuleSection> FModuleMap::ParseSections(const FProcess& Process, const MODULEENTRY32& Entry)
{
	std::vector<FModuleSection> Sections;

	IMAGE_DOS_HEADER DosHeader{};
	IMAGE_NT_HEADERS NtHeaders{};
	ReadProcessMemory(Process.ProcessHandle, Entry.modBaseAddr, &DosHeader, sizeof(DosHeader), nullptr);
	ReadProcessMemory(Process.ProcessHandle,
		reinterpret_cast<void*>((uintptr_t)Entry.modBaseAddr + DosHeader.e_lfanew),
		&NtHeaders, sizeof(NtHeaders), nullptr);

	std::vector<IMAGE_SECTION_HEADER> Headers(NtHeaders.FileHeader.NumberOfSections);
	ReadProcessMemory(Process.ProcessHandle,
		reinterpret_cast<void*>((uintptr_t)Entry.modBaseAddr + DosHeader.e_lfanew + sizeof(IMAGE_NT_HEADERS)),
		Headers.data(), sizeof(IMAGE_SECTION_HEADER) * Headers.size(), nullptr);

	for (const auto& Header : Headers)
	{
		FModuleSection Section;
		Section.Name = reinterpret_cast<const char*>(Header.Name);
		Section.Start = (uintptr_t)Entry.modBaseAddr + Header.VirtualAddress;
		Section.End = Section.Start + Header.Misc.VirtualSize;
		Section.bFlagExecutable = Header.Characteristics & IMAGE_SCN_MEM_EXECUTE;
		Section.bFlagReadonly = Header.Characteristics & IMAGE_SCN_MEM_READ;
		Sections.push_back(std::move(Section));
	}

	return Sections;
}


FTargetProcess::FTargetProcess(const std::string& InProcessName) : Process(InProcessName), MemoryMap(Process), ModuleMap(Process) {}

FTargetProcess::FTargetProcess(DWORD InPID) : Process(InPID), MemoryMap(Process), ModuleMap(Process) {}

FTargetProcess::FTargetProcess(const FProcess& InProcess) : Process(InProcess), MemoryMap(Process), ModuleMap(Process) {}

bool FProcess::IsValid() const
{
	return ProcessHandle != INVALID_HANDLE_VALUE;
}

bool FTargetProcess::IsValid() const
{
	return Process.IsValid() && !ModuleMap.Modules.empty();
}

DWORD FTargetProcess::SetProtection(uintptr_t address, size_t size, DWORD protection)
{
	DWORD oldProtection;
	VirtualProtectEx(Process.ProcessHandle, reinterpret_cast<void*>(address), size, protection, &oldProtection);
	return oldProtection;
}

FModule* FTargetProcess::GetModule(const std::string& moduleName)
{
	auto Matches = [&](const FModule& Module) { return Module.Name.find(moduleName) != std::string::npos; };

	auto it = std::find_if(ModuleMap.Modules.begin(), ModuleMap.Modules.end(), Matches);
	if (it != ModuleMap.Modules.end())
	{
		return &(*it);
	}

	return nullptr;
}

FModuleSection* FTargetProcess::GetModuleSection(uintptr_t Address)
{
	auto Matches = [&](const FModuleSection& Section) { return Section.Contains(Address); };

	for (auto& Module : ModuleMap.Modules)
	{
		auto it = std::find_if(Module.Sections.begin(), Module.Sections.end(), Matches);
		if (it != Module.Sections.end())
		{
			return &(*it);
		}
	}

	return nullptr;
}

std::future<FMemoryBlock> FTargetProcess::ReadMemoryAsync(const FMemoryRange& Range)
{
	return std::async(std::launch::async, [Range, &Process = Process]() {
		FMemoryBlock Block(Range.Start, Range.Size());

		if (Block.IsValid())
		{
			ReadProcessMemory(Process.ProcessHandle, Block.Address, Block.Copy.data(), Block.Size, nullptr);
		}

		return Block;
		});
}

FMemoryRange* FTargetProcess::GetMemoryRange(const uintptr_t Address)
{
	auto Matches = [&](const FMemoryRange& Range) { return Range.Contains(Address); };

	auto it = std::find_if(MemoryMap.Ranges.begin(), MemoryMap.Ranges.end(), Matches);
	if (it != MemoryMap.Ranges.end())
	{
		return &(*it);
	}

	return nullptr;
}

std::vector<FMemoryBlock> FTargetProcess::GetReadableMemoryBlocking()
{
	std::vector<std::future<FMemoryBlock>> Futures;
	Futures.reserve(MemoryMap.Ranges.size());

	for (const auto& Range : MemoryMap.Ranges)
	{
		if (Range.bReadable)
		{
			Futures.emplace_back(ReadMemoryAsync(Range));
		}
	}

	std::vector<FMemoryBlock> Blocks;
	Blocks.reserve(Futures.size());
	std::transform(Futures.begin(), Futures.end(), std::back_inserter(Blocks), [](auto& Future) { return Future.get(); });

	return Blocks;
}

std::vector<std::future<FMemoryBlock>> FTargetProcess::AsyncGetReadableMemory()
{
	std::vector<std::future<FMemoryBlock>> Futures;

	for (const auto& Range : MemoryMap.Ranges)
	{
		if (Range.bReadable && !Range.bExecutable)
		{
			Futures.emplace_back(ReadMemoryAsync(Range));
		}
	}

	return Futures;
}

std::vector<std::future<FMemoryBlock>> FTargetProcess::AsyncGetExecutableMemory()
{
	std::vector<std::future<FMemoryBlock>> Futures;

	for (const auto& Range : MemoryMap.Ranges)
	{
		if (Range.bExecutable)
		{
			Futures.emplace_back(ReadMemoryAsync(Range));
		}
	}

	return Futures;
}

void FTargetProcess::Read(uintptr_t Address, void* Buffer, size_t Size)
{
	if (!ReadProcessMemory(Process.ProcessHandle, reinterpret_cast<void*>(Address), Buffer, Size, NULL))
	{
		printf("ReadProcessMemory failed: %d\n", GetLastError());
	}
}

std::future<std::vector<uint8_t>> FTargetProcess::AsyncRead(uintptr_t Address, size_t Size)
{
	return std::async(std::launch::async,
		[this, Address, Size]()
		{
			std::vector<uint8_t> Buffer(Size);
			if (!Buffer.empty())
			{
				ReadProcessMemory(Process.ProcessHandle, reinterpret_cast<void*>(Address), Buffer.data(), Size, NULL);
			}
			return Buffer;
		});
}

bool FTargetProcess::Write(uintptr_t Address, void* Buffer, size_t Size)
{
	return WriteProcessMemory(Process.ProcessHandle, reinterpret_cast<void*>(Address), Buffer, Size, NULL);
}

void FTargetProcess::AsyncWrite(uintptr_t Address, void* Buffer, size_t Size)
{
	auto result =
		std::async(std::launch::async, [this, Address, Buffer, Size]() { WriteProcessMemory(Process.ProcessHandle, reinterpret_cast<void*>(Address), Buffer, Size, NULL); });
}

HANDLE FTargetProcess::InjectDLL(const std::string& DllPath)
{
	HANDLE DllInjectThreadHandle = NULL;

	HMODULE Kernel32 = GetModuleHandleA("kernel32.dll");
	if (!Kernel32)
	{
		return DllInjectThreadHandle;
	}

	LPVOID LoadLibraryAAddr = (LPVOID)GetProcAddress(Kernel32, "LoadLibraryA");
	if (!LoadLibraryAAddr)
	{
		return DllInjectThreadHandle;
	}

	LPVOID RemoteString = (LPVOID)VirtualAllocEx(Process.ProcessHandle, NULL, DllPath.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!RemoteString)
	{
		return DllInjectThreadHandle;
	}

	if (!WriteProcessMemory(Process.ProcessHandle, (LPVOID)RemoteString, DllPath.c_str(), DllPath.size(), NULL))
	{
	}

	DllInjectThreadHandle = CreateRemoteThread(Process.ProcessHandle, NULL, NULL, (LPTHREAD_START_ROUTINE)LoadLibraryAAddr, (LPVOID)RemoteString, NULL, NULL);
	return DllInjectThreadHandle;
}

std::future<HANDLE> FTargetProcess::InjectDLLAsync(const std::string& DllPath)
{
	return std::async(std::launch::async, [this, DllPath]() { return InjectDLL(DllPath); });
}