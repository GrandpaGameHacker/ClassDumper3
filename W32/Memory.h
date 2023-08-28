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

constexpr int StandardBuffer = 0x1000;

struct FProcessListItem
{
	DWORD PID;
	std::string Name;
	std::string Path;
};

std::vector<FProcessListItem> GetProcessList();
std::vector<FProcessListItem> GetProcessList(const std::string& Filter);

void GetDebugPrivilege();

bool Is32BitExecutable(const std::string& FilePath, bool& bFailed);
bool Is32BitProcess(DWORD PID);

bool IsSameBitsProcess(const std::string& FilePath);
inline bool IsRunning64Bits() {return sizeof(void*) == 8;};


struct FProcess
{
	HANDLE ProcessHandle;
	DWORD PID;
	std::string ProcessName;

	DEBUG_EVENT DebugEvent;
	bool bIsDebugActive = false;
	CONTEXT DebugActiveContext;

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
	uintptr_t Start;
	uintptr_t End;
	bool bExecutable, bReadable, bWritable;

	FMemoryRange(uintptr_t InStart, uintptr_t InEnd, bool InbExecutable, bool InbReadable, bool InbWritable);
	bool Contains(uintptr_t Address) const;
	uintptr_t Size() const;
};

struct FMemoryMap
{
	std::vector<FMemoryRange> Ranges;
	FMemoryRange* CurrentRange = nullptr;
	void Setup(FProcess* Process);
};


struct FModuleSection
{
	uintptr_t Start = 0;
	uintptr_t End = 0;
	bool bFlagReadonly = false;
	bool bFlagExecutable = false;
	std::string Name;
	
	bool Contains(uintptr_t Address) const;
	uintptr_t Size() const;
};

struct FModule
{
	void* baseAddress = nullptr;
	std::vector<FModuleSection> sections;
	std::string name;
};

struct FModuleMap
{
	std::vector<FModule> Modules;
	void Setup(FProcess* process);

	FModule* GetModule(const char* name);
	FModule* GetModule(const std::string& name);
	FModule* GetModule(uintptr_t address);
};

struct MemoryBlock
{
	void* blockCopy;
	void* blockAddress;
	size_t blockSize;

	~MemoryBlock()
	{
		free(blockCopy);
	}
};

struct FTargetProcess
{
	FProcess process;
	FMemoryMap memoryMap;
	FModuleMap moduleMap;

	void Setup(const std::string& processName);
	void Setup(DWORD processID);
	void Setup(FProcess process);

	/*********** Process Utils ***********/
	bool IsValid();

	FModule* GetModule(const std::string& moduleName);
	FMemoryRange* GetMemoryRange(uintptr_t address);
	std::vector<MemoryBlock> GetReadableMemory();
	std::vector<std::future<MemoryBlock>> AsyncGetReadableMemory();
	FModuleSection* GetModuleSection(uintptr_t address);
	DWORD SetProtection(uintptr_t address, size_t size, DWORD protection);

	/*********** Window Utils ***********/
	
	std::vector<HWND> GetWindows();
	std::string GetWindowName(HWND window);
	bool SetWindowName(HWND window, const std::string& name);
	bool SetTransparency(HWND window, BYTE alpha);

	/*********** Memory Utils ***********/

	void Read(uintptr_t address, void* buffer, size_t size);

	std::future<void*> AsyncRead(uintptr_t address, size_t size);

	template<typename T>
	T Read(uintptr_t address)
	{
		T buffer;
		ReadProcessMemory(process.ProcessHandle, (void*)address, &buffer, sizeof(T), NULL);
		return buffer;
	}

	template<typename T>
	std::future<T> AsyncRead(uintptr_t address)
	{
		return std::async(std::launch::async, [this, address]()
			{
				T buffer;
				ReadProcessMemory(process.ProcessHandle, (void*)address, &buffer, sizeof(T), NULL);
				return buffer;
			});
	}

	void Write(uintptr_t address, void* buffer, size_t size);
	void AsyncWrite(uintptr_t address, void* buffer, size_t size);

	template<typename T>
	void Write(uintptr_t address, T value)
	{
		WriteProcessMemory(process.ProcessHandle, (void*)address, &value, sizeof(T), NULL);
	}

	HANDLE InjectDLL(const std::string& dllPath);
	void InjectDLLAsync(const std::string& dllPath);
	
};


template <typename T>
class RemoteVariable
{
public:
	RemoteVariable(FTargetProcess* process, T value)
	{
		this->process = process;
		this->value = value;
		this->address = (uintptr_t)VirtualAllocEx(process->process.ProcessHandle, NULL, sizeof(T), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
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
		VirtualFreeEx(process->process.ProcessHandle, (void*)address, sizeof(T), MEM_RELEASE);
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
		return CreateRemoteThread(process->process.ProcessHandle, NULL, NULL, (LPTHREAD_START_ROUTINE)address, NULL, NULL, NULL);
	}

	HANDLE operator()(void* args)
	{
		return CreateRemoteThread(process->process.ProcessHandle, NULL, NULL, (LPTHREAD_START_ROUTINE)address, args, NULL, NULL);
	}

	// function to allocate arguments for injected code, its up to the injected code to unpack the arguments if it is a struct
	template<typename T>
	void* AllocArgs(T args)
	{
		void* argsAddress = VirtualAllocEx(process->process.ProcessHandle, NULL, sizeof(T), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		process->Write(argsAddress, &args, sizeof(T));
		return argsAddress;
	};

	template<typename T>
	void WaitAndFreeArgs(void* argsAddress, HANDLE hThread)
	{
		WaitForSingleObject(hThread.Get(), INFINITE);
		VirtualFreeEx(process->process.ProcessHandle, argsAddress, sizeof(T), MEM_RELEASE);
	}

private:
	FTargetProcess* process;
	uintptr_t address;
};