#pragma once

// Windows-specific headers
#include <Windows.h>
#include <tlhelp32.h>

// Standard library headers
#include <atomic>
#include <future>
#include <map>
#include <string>
#include <vector>

// Constants
constexpr int StandardBufferSize = 0x1000;
constexpr ULONG SE_DEBUG_PRIVILEGE = 20;

// ---------------------------------------------
// Process Listing / Utility
// ---------------------------------------------

struct FProcessListItem {
	DWORD PID;
	std::string Name;
	std::string ProcessListName;
	std::string Path;
};

bool IgnoreProcess(const std::string& ProcessName);
std::vector<FProcessListItem> GetProcessList(const std::string& Filter);

bool GetDebugPrivilege();
bool Is32BitExecutable(const std::string& FilePath, bool& bFailed);
bool IsSameBitsProcess(const std::string& FilePath);
inline bool IsRunning64Bits() { return sizeof(void*) == 8; }

// ---------------------------------------------
// Process
// ---------------------------------------------

struct FProcess {
	HANDLE ProcessHandle = INVALID_HANDLE_VALUE;
	DWORD PID = 0;
	std::string ProcessName = "<invalid>";

	FProcess();
	explicit FProcess(DWORD InPID);
	explicit FProcess(const std::string& InProcessName);
	FProcess(HANDLE InProcessHandle, DWORD InPID, const std::string& InProcessName);
	FProcess& operator=(const FProcess& Other);
	FProcess(const FProcess& Other);

	DWORD GetProcessID(const std::string& ProcessName);
	bool IsValid() const;

	void* AllocRW(size_t size);
	void* AllocRWX(size_t size);
	bool Free(void* Address);
};

// ---------------------------------------------
// Memory
// ---------------------------------------------

struct FMemoryRange {
	uintptr_t Start = 0;
	uintptr_t End = 0;
	bool bExecutable = false;
	bool bReadable = false;
	bool bWritable = false;

	FMemoryRange() = default;
	FMemoryRange(uintptr_t InStart, uintptr_t InEnd, bool InbExecutable, bool InbReadable, bool InbWritable);

	bool Contains(uintptr_t Address) const;
	uintptr_t Size() const;
};

struct FMemoryMap {
	std::vector<FMemoryRange> Ranges;

	FMemoryMap() = default;
	explicit FMemoryMap(const FProcess& Process);
};

/** used to copy blocks of memory from remote process, while keeping track of the original address */
struct FMemoryBlock {
	void* Address = nullptr;
	size_t Size = 0;
	std::vector<uint8_t> Copy;

	FMemoryBlock() = default;
	FMemoryBlock(void* InAddress, size_t InSize);
	FMemoryBlock(uintptr_t InAddress, size_t InSize);

	bool IsValid() const {
		return Address != nullptr && Copy.size() == Size && Size > 0;
	}
};

// ---------------------------------------------
// Modules
// ---------------------------------------------

struct FModuleSection
{
	FModuleSection() = default;
	FModuleSection(uintptr_t InStart, uintptr_t InEnd, bool InbFlagReadonly, bool InbFlagExecutable, const std::string& InName);
	FModuleSection(const FModuleSection& Other);

	bool Contains(uintptr_t Address) const;
	uintptr_t Size() const;

	uintptr_t Start = 0;
	uintptr_t End = 0;
	bool bFlagReadonly = false;
	bool bFlagExecutable = false;
	std::string Name;
};

struct FModule
{
	FModule() = default;
	FModule(void* InBaseAddress, const std::vector<FModuleSection>& InSections, const std::string& InName);
	FModule(const FModule& Other);

	void* BaseAddress = nullptr;
	std::vector<FModuleSection> Sections;
	std::string Name;
};

struct FModuleMap {
	FModuleMap() = default;
	explicit FModuleMap(const FProcess& Process);

	FModule* GetModule(const char* Name);
	FModule* GetModule(const std::string& Name);
	FModule* GetModule(uintptr_t Address);

	std::vector<FModule> Modules;
private:
	void LoadModules(const FProcess& Process);
	FModule ParseModule(const FProcess& Process, const MODULEENTRY32& Entry);
	std::vector<FModuleSection> ParseSections(const FProcess& Process, const MODULEENTRY32& Entry);
};

// ---------------------------------------------
// Target Process
// ---------------------------------------------

struct FTargetProcess {
	FProcess Process;
	FMemoryMap MemoryMap;
	FModuleMap ModuleMap;

	FTargetProcess() = default;
	explicit FTargetProcess(const std::string& InProcessName);
	explicit FTargetProcess(DWORD InPID);
	explicit FTargetProcess(const FProcess& InProcess);

	// Process
	bool IsValid() const;
	DWORD SetProtection(uintptr_t Address, size_t Size, DWORD Protection);

	// Modules
	FModule* GetModule(const std::string& moduleName);
	inline std::vector<FModule>& GetModules() { return ModuleMap.Modules; }
	FModuleSection* GetModuleSection(uintptr_t Address);

	// Memory
	std::future<FMemoryBlock> ReadMemoryAsync(const FMemoryRange& Range); // Async Helper
	FMemoryRange* GetMemoryRange(uintptr_t Address);
	std::vector<FMemoryBlock> GetReadableMemoryBlocking();
	std::vector<std::future<FMemoryBlock>> AsyncGetReadableMemory();
	std::vector<std::future<FMemoryBlock>> AsyncGetExecutableMemory();

	void Read(uintptr_t Address, void* Buffer, size_t Size);

	std::future<std::vector<uint8_t>> AsyncRead(uintptr_t Address, size_t Size);

	template<typename T>
	T Read(uintptr_t Address) {
		T Buffer;
		ReadProcessMemory(Process.ProcessHandle, reinterpret_cast<void*>(Address), &Buffer, sizeof(T), nullptr);
		return Buffer;
	}

	template<typename T>
	std::future<T> AsyncRead(uintptr_t Address) {
		return std::async(std::launch::async, [this, Address]() {
			T Buffer;
			ReadProcessMemory(Process.ProcessHandle, reinterpret_cast<void*>(Address), &Buffer, sizeof(T), nullptr);
			return Buffer;
			});
	}

	bool Write(uintptr_t Address, void* Buffer, size_t Size);
	void AsyncWrite(uintptr_t Address, void* Buffer, size_t Size);

	template<typename T>
	bool Write(uintptr_t Address, T Value) {
		return WriteProcessMemory(Process.ProcessHandle, reinterpret_cast<void*>(Address), &Value, sizeof(T), nullptr);
	}

	// DLL Injection (Basic)
	HANDLE InjectDLL(const std::string& DllPath);
	std::future<HANDLE> InjectDLLAsync(const std::string& DllPath);
};