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

constexpr int bufferSize = 0x1000;

struct ProcessListItem
{
	DWORD pid;
	std::string name;
	std::string path;
};

std::vector<ProcessListItem> GetProcessList();
std::vector<ProcessListItem> GetProcessList(std::string filter);
float ShannonEntropyBlock(const void* data, size_t size);
bool IsBlockHighEntropy(const void* data, size_t size, float threshold);

void GetDebugPrivilege();

bool Is32BitExecutable(const std::string& filePath, bool& bFailed);
bool Is32BitProcess(DWORD dwProcessID);

bool IsSameBitsProcess(const std::string& FilePath);


struct Process
{
	HANDLE hProcess;
	DWORD dwProcessID;
	std::string ProcessName;

	DEBUG_EVENT debugEvent;
	bool bIsDebugActive = false;
	CONTEXT debugActiveContext;

	Process();
	Process(DWORD dwProcessID);
	Process(const std::string& ProcessName);
	Process(HANDLE hProcess, DWORD dwProcessID, const std::string& ProcessName);
	Process& operator=(const Process& other);
	Process(const Process& other);
	
	DWORD GetProcessID(const std::string& ProcessName);
	bool IsValid();
	bool Is32Bit();
	bool IsSameBits();
	bool AttachDebugger();
	bool DetachDebugger();
	bool DebugWait();
	bool DebugContinue(DWORD dwContinueStatus);
	void* AllocRW(size_t size);
	void* AllocRWX(size_t size);
	bool Free(void* Address);

private:
	void CheckBits();

};

struct MemoryRange
{
	uintptr_t start;
	uintptr_t end;
	bool bExecutable, bReadable, bWritable;

	MemoryRange(uintptr_t start, uintptr_t end, bool bExecutable, bool bReadable, bool bWritable);
	bool Contains(uintptr_t address);
	uintptr_t Size();
};

struct MemoryMap
{
	std::vector<MemoryRange> ranges;
	MemoryRange* currentRange = nullptr;
	void Setup(Process* process);
};


struct ModuleSection
{
	uintptr_t start = 0;
	uintptr_t end = 0;
	bool bFlagReadonly = false;
	bool bFlagExecutable = false;
	std::string name;
	
	bool Contains(uintptr_t address);
	uintptr_t Size();
};

struct Module
{
	void* baseAddress = nullptr;
	std::vector<ModuleSection> sections;
	std::string name;
};

struct ModuleMap
{
	std::vector<Module> modules;
	void Setup(Process* process);

	Module* GetModule(const char* name);
	Module* GetModule(const std::string& name);
	Module* GetModule(uintptr_t address);
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

struct TargetProcess
{
	Process process;
	MemoryMap memoryMap;
	ModuleMap moduleMap;

	void Setup(const std::string& processName);
	void Setup(DWORD processID);
	void Setup(Process process);

	/*********** Process Utils ***********/

	bool Is64Bit();
	bool IsValid();

	Module* GetModule(const std::string& moduleName);
	MemoryRange* GetMemoryRange(uintptr_t address);
	std::vector<MemoryBlock> GetReadableMemory();
	std::vector<std::future<MemoryBlock>> AsyncGetReadableMemory();
	ModuleSection* GetModuleSection(uintptr_t address);
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
		ReadProcessMemory(process.hProcess, (void*)address, &buffer, sizeof(T), NULL);
		return buffer;
	}

	template<typename T>
	std::future<T> AsyncRead(uintptr_t address)
	{
		return std::async(std::launch::async, [this, address]()
			{
				T buffer;
				ReadProcessMemory(process.hProcess, (void*)address, &buffer, sizeof(T), NULL);
				return buffer;
			});
	}

	void Write(uintptr_t address, void* buffer, size_t size);
	void AsyncWrite(uintptr_t address, void* buffer, size_t size);

	template<typename T>
	void Write(uintptr_t address, T value)
	{
		WriteProcessMemory(process.hProcess, (void*)address, &value, sizeof(T), NULL);
	}

	HANDLE InjectDLL(const std::string& dllPath);
	void InjectDLLAsync(const std::string& dllPath);
	
};


template <typename T>
class RemoteVariable
{
public:
	RemoteVariable(TargetProcess* process, T value)
	{
		this->process = process;
		this->value = value;
		this->address = (uintptr_t)VirtualAllocEx(process->process.hProcess, NULL, sizeof(T), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		this->Write();
	}

	RemoteVariable(TargetProcess* process, uintptr_t address)
	{
		this->process = process;
		this->address = address;
		this->Read();
	}

	~RemoteVariable()
	{
		VirtualFreeEx(process->process.hProcess, (void*)address, sizeof(T), MEM_RELEASE);
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

	TargetProcess* process;
	uintptr_t address;
	T value;
};

class RemotePointer
{
public:

	RemotePointer() : process(nullptr), address(0)
	{

	}

	RemotePointer(TargetProcess* process, uintptr_t address)
	{
		Setup(process, address);
	}

	RemotePointer(TargetProcess* process, uintptr_t address, uintptr_t offset)
	{
		Setup(process, address, offset);
	}

	RemotePointer(TargetProcess* process, uintptr_t address, std::vector<uintptr_t> offsets)
	{
		Setup(process, address, offsets);
	}

	void Setup(TargetProcess* process, uintptr_t address)
	{
		this->process = process;
		this->address = address;
	}

	void Setup(TargetProcess* process, uintptr_t address, std::vector<uintptr_t> offsets)
	{
		this->process = process;
		this->address = address;
		this->offsets = offsets;
	}

	void Setup(TargetProcess* process, uintptr_t address, uintptr_t offset)
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

	TargetProcess* process;
	uintptr_t address;
	std::vector<uintptr_t> offsets;
};

class RemoteFunction
{
public:
	RemoteFunction(TargetProcess* process, uintptr_t address)
	{
		this->process = process;
		this->address = address;
	}

	HANDLE operator()()
	{
		return CreateRemoteThread(process->process.hProcess, NULL, NULL, (LPTHREAD_START_ROUTINE)address, NULL, NULL, NULL);
	}

	HANDLE operator()(void* args)
	{
		return CreateRemoteThread(process->process.hProcess, NULL, NULL, (LPTHREAD_START_ROUTINE)address, args, NULL, NULL);
	}

	// function to allocate arguments for injected code, its up to the injected code to unpack the arguments if it is a struct
	template<typename T>
	void* AllocArgs(T args)
	{
		void* argsAddress = VirtualAllocEx(process->process.hProcess, NULL, sizeof(T), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		process->Write(argsAddress, &args, sizeof(T));
		return argsAddress;
	};

	template<typename T>
	void WaitAndFreeArgs(void* argsAddress, HANDLE hThread)
	{
		WaitForSingleObject(hThread.Get(), INFINITE);
		VirtualFreeEx(process->process.hProcess, argsAddress, sizeof(T), MEM_RELEASE);
	}

private:
	TargetProcess* process;
	uintptr_t address;
};