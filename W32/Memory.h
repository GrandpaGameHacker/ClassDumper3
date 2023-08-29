// windows
#pragma once 
#include <Windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <DbgHelp.h>
#include <psapi.h>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib,"dbghelp.lib")

#include <thread>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>
#include <iostream>
#include <string>
#include <future>
#include <map>
#include <algorithm>

constexpr int StandardBufferSize = 0x1000;

struct FProcessListItem
{
	DWORD PID;
	std::string Name;
	std::string Path;
};

bool ShouldIgnoreProcess(const std::string& ProcessName);
std::vector<FProcessListItem> GetProcessList();
std::vector<FProcessListItem> GetProcessList(const std::string& Filter);

void GetDebugPrivilege();

bool Is32BitExecutable(const std::string& FilePath, bool& bFailed);
bool Is32BitProcess(DWORD PID);

bool IsSameBitsProcess(const std::string& FilePath);
inline bool IsRunning64Bits() {return sizeof(void*) == 8;};


struct FProcess
{
	HANDLE ProcessHandle = INVALID_HANDLE_VALUE;
	DWORD PID = 0;
	std::string ProcessName = "<invalid>";

	DEBUG_EVENT DebugEvent = { 0 };
	bool bIsDebugActive = false;
	CONTEXT DebugActiveContext = { 0 };

	FProcess();
	FProcess(DWORD InPID);
	FProcess(const std::string& InProcessName);
	FProcess(HANDLE InProcessHandle, DWORD InPID, const std::string& InProcessName);
	FProcess& operator=(const FProcess& Other);
	FProcess(const FProcess& other);
	
	DWORD GetProcessID(const std::string& ProcessName);
	bool IsValid();
	bool AttachDebugger();
	bool DetachDebugger();
	bool DebugWait();
	bool DebugContinue(DWORD ContinueStatus);
	void* AllocRW(size_t size);
	void* AllocRWX(size_t size);
	bool Free(void* Address);
};

struct FMemoryRange
{
	FMemoryRange()
		: Start(0)
		, End(0)
		, bExecutable(false)
		, bReadable(false)
		, bWritable(false)
	{}
	
	uintptr_t Start;
	uintptr_t End;
	bool bExecutable, bReadable, bWritable;

	FMemoryRange(uintptr_t InStart, uintptr_t InEnd, bool InbExecutable, bool InbReadable, bool InbWritable);
	bool Contains(uintptr_t Address) const;
	uintptr_t Size() const;
};

struct FMemoryMap
{
	std::vector<FMemoryRange> Ranges {};
	FMemoryRange* CurrentRange = nullptr;
	void Setup(FProcess* Process);
};


struct FModuleSection
{
	FModuleSection()
		: Start(0)
		, End(0)
		, bFlagReadonly(false)
		, bFlagExecutable(false)
		, Name("")
	{}

	FModuleSection(uintptr_t InStart, uintptr_t InEnd, bool InbFlagReadonly, bool InbFlagExecutable, const std::string& InName)
		: Start(InStart)
		, End(InEnd)
		, bFlagReadonly(InbFlagReadonly)
		, bFlagExecutable(InbFlagExecutable)
		, Name(InName)
	{}

	FModuleSection(const FModuleSection& Other)
		: Start(Other.Start)
		, End(Other.End)
		, bFlagReadonly(Other.bFlagReadonly)
		, bFlagExecutable(Other.bFlagExecutable)
		, Name(Other.Name)
	{}
	
	uintptr_t Start;
	uintptr_t End;
	bool bFlagReadonly;
	bool bFlagExecutable;
	std::string Name;
	
	bool Contains(uintptr_t Address) const;
	uintptr_t Size() const;
};

struct FModule
{
	FModule()
		: BaseAddress(nullptr)
		, Sections()
		, Name("")
	{}

	FModule(void* InBaseAddress, const std::vector<FModuleSection>& InSections, const std::string& InName)
		: BaseAddress(InBaseAddress)
		, Sections(InSections)
		, Name(InName)
	{}

	FModule(const FModule& Other)
		: BaseAddress(Other.BaseAddress)
		, Sections(Other.Sections)
		, Name(Other.Name)
	{}
	
	void* BaseAddress = nullptr;
	std::vector<FModuleSection> Sections;
	std::string Name;
};

struct FModuleMap
{
	std::vector<FModule> Modules;
	void Setup(const FProcess* Process);

	FModule* GetModule(const char* Name);
	FModule* GetModule(const std::string& Name);
	FModule* GetModule(const uintptr_t Address);
};

struct FMemoryBlock
{

	FMemoryBlock() :
		Copy(nullptr),
		Address(nullptr),
		Size(0)
	{}

	FMemoryBlock(void* InAddress, size_t InSize) :
		Address(InAddress),
		Size(InSize)
	{
		Copy = malloc(Size);
	}

	FMemoryBlock(uintptr_t InAddress, size_t InSize) :
		Address(reinterpret_cast<void*>(InAddress)),
		Size(InSize)
	{
		Copy = malloc(Size);
	}

	FMemoryBlock(const FMemoryBlock& Other) :
		Copy(Other.Copy),
		Address(Other.Address),
		Size(Other.Size)
	{}
	
	void FreeBlock()
	{
		if (Copy)
		{
			free(Copy);
			Copy = nullptr;
		}
	}

	void* Copy;
	void* Address;
	size_t Size;
};

struct FTargetProcess
{
	FProcess Process;
	FMemoryMap MemoryMap;
	FModuleMap ModuleMap;

	void Setup(const std::string& InProcessName);
	void Setup(DWORD InPID);
	void Setup(const FProcess& InProcess);

	/*********** Process Utils ***********/
	bool IsValid();

	FModule* GetModule(const std::string& moduleName);
	FMemoryRange* GetMemoryRange(const uintptr_t Address);
	std::vector<FMemoryBlock> GetReadableMemory();
	std::vector<std::future<FMemoryBlock>> AsyncGetReadableMemory(bool bExecutable = false);
	FModuleSection* GetModuleSection(uintptr_t address);
	DWORD SetProtection(uintptr_t address, size_t size, DWORD protection);

	/*********** Window Utils ***********/
	
	std::vector<HWND> GetWindows();
	std::string GetWindowName(HWND Window);
	bool SetWindowName(HWND Window, const std::string& Name);
	bool SetTransparency(HWND Window, BYTE Alpha);

	/*********** Memory Utils ***********/

	void Read(uintptr_t Address, void* Buffer, size_t Size);

	std::future<void*> AsyncRead(uintptr_t Address, size_t Size);

	template<typename T>
	T Read(uintptr_t Address)
	{
		T Buffer;
		ReadProcessMemory(Process.ProcessHandle, (void*)Address, &Buffer, sizeof(T), NULL);
		return Buffer;
	}

	template<typename T>
	std::future<T> AsyncRead(uintptr_t address)
	{
		return std::async(std::launch::async, [this, address]()
			{
				T buffer;
				ReadProcessMemory(Process.ProcessHandle, (void*)address, &buffer, sizeof(T), NULL);
				return buffer;
			});
	}

	void Write(uintptr_t address, void* buffer, size_t size);
	void AsyncWrite(uintptr_t address, void* buffer, size_t size);

	template<typename T>
	void Write(uintptr_t address, T value)
	{
		WriteProcessMemory(Process.ProcessHandle, (void*)address, &value, sizeof(T), NULL);
	}

	HANDLE InjectDLL(const std::string& DllPath);
	std::future<HANDLE> InjectDLLAsync(const std::string& DllPath);
	
};


template <typename T>
class RemoteVariable
{
public:
	RemoteVariable(FTargetProcess* process, T value)
	{
		this->process = process;
		this->value = value;
		this->address = (uintptr_t)VirtualAllocEx(process->Process.ProcessHandle, NULL, sizeof(T), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		this->Write();
	}

	RemoteVariable(FTargetProcess* process, uintptr_t address)
	{
		this->process = process;
		this->address = address;
		this->Read();
	}

	~RemoteVariable()
	{
		VirtualFreeEx(process->Process.ProcessHandle, (void*)address, sizeof(T), MEM_RELEASE);
	}

	T operator=(T value)
	{
		this->value = value;
		this->Write();
		return value;
	}

	operator T()
	{
		this->Read();
		return value;
	}

	void Read()
	{
		process->Read(address, &value, sizeof(T));
	}

	void Write()
	{
		process->Write(address, &value, sizeof(T));
	}

	FTargetProcess* process;
	uintptr_t address;
	T value;
};

class RemotePointer
{
public:

	RemotePointer() : process(nullptr), address(0)
	{

	}

	RemotePointer(FTargetProcess* process, uintptr_t address)
	{
		Setup(process, address);
	}

	RemotePointer(FTargetProcess* process, uintptr_t address, uintptr_t offset)
	{
		Setup(process, address, offset);
	}

	RemotePointer(FTargetProcess* process, uintptr_t address, std::vector<uintptr_t> offsets)
	{
		Setup(process, address, offsets);
	}

	void Setup(FTargetProcess* process, uintptr_t address)
	{
		this->process = process;
		this->address = address;
	}

	void Setup(FTargetProcess* process, uintptr_t address, std::vector<uintptr_t> offsets)
	{
		this->process = process;
		this->address = address;
		this->offsets = offsets;
	}

	void Setup(FTargetProcess* process, uintptr_t address, uintptr_t offset)
	{
		this->process = process;
		this->address = address;
		this->offsets.push_back(offset);
	}


	RemotePointer operator[](uintptr_t offset)
	{
		offsets.push_back(offset);
		return *this;
	}

	template<typename T>
	T Read()
	{
		T value;
		if (offsets.size() > 0)
		{
			process->Read(GetAddress(), &value, sizeof(T));
		}
		else
		{
			process->Read(address, &value, sizeof(T));
		}
		return value;
	}

	template<typename T>
	void Write(T value)
	{
		if (offsets.size() > 0)
		{
			process->Write(GetAddress(), &value, sizeof(T));
		}
		else
		{
			process->Write(address, &value, sizeof(T));
		}
	}

	template<typename T>
	operator T()
	{
		return Read<T>();
	}

	template<typename T>
	T operator=(T value)
	{
		Write<T>(value);
		return value;
	}

	// function that reads a pointer to a pointer and returns a RemotePointer
	RemotePointer GetPointer()
	{
		return RemotePointer(process, Read<uintptr_t>());
	}

	RemotePointer GetPointer(uintptr_t offset)
	{
		return RemotePointer(process, Read<uintptr_t>(), { offset });
	}

	RemotePointer GetPointer(std::vector<uintptr_t> offsets)
	{
		return RemotePointer(process, Read<uintptr_t>(), offsets);
	}

private:
	uintptr_t GetAddress()
	{
		uintptr_t address = this->address;

		for (auto& offset : offsets)
		{
			address = process->Read<uintptr_t>(address) + offset;
		}

		return address;
	}

	FTargetProcess* process;
	uintptr_t address;
	std::vector<uintptr_t> offsets;
};

class RemoteFunction
{
public:
	RemoteFunction(FTargetProcess* process, uintptr_t address)
	{
		this->process = process;
		this->address = address;
	}

	HANDLE operator()()
	{
		return CreateRemoteThread(process->Process.ProcessHandle, NULL, NULL, (LPTHREAD_START_ROUTINE)address, NULL, NULL, NULL);
	}

	HANDLE operator()(void* args)
	{
		return CreateRemoteThread(process->Process.ProcessHandle, NULL, NULL, (LPTHREAD_START_ROUTINE)address, args, NULL, NULL);
	}

	// function to allocate arguments for injected code, its up to the injected code to unpack the arguments if it is a struct
	template<typename T>
	void* AllocArgs(T args)
	{
		void* argsAddress = VirtualAllocEx(process->Process.ProcessHandle, NULL, sizeof(T), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		process->Write(argsAddress, &args, sizeof(T));
		return argsAddress;
	};

	template<typename T>
	void WaitAndFreeArgs(void* argsAddress, HANDLE hThread)
	{
		WaitForSingleObject(hThread.Get(), INFINITE);
		VirtualFreeEx(process->Process.ProcessHandle, argsAddress, sizeof(T), MEM_RELEASE);
	}

private:
	FTargetProcess* process;
	uintptr_t address;
};