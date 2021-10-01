#pragma once
#include <Windows.h>
#include <vector>
#include <future>
#include <thread>
#include <mutex>

namespace Memory
{
	//ReadRemoteStructure<T>
	template<class T>
	bool RRS(HANDLE hProcess, T& Structure, void* address)
	{
		SIZE_T bytesRead = 0;
		if (ReadProcessMemory(hProcess, address, (void*)Structure, sizeof(T), &bytesRead) == 0)
		{
			return false;
		}
		return true;
	}

	void* TranslateToRemote(void* localBase, void* local, void* remote);
	bool IsBadReadPointer(HANDLE hProcess, void* p);
	bool IsBadReadPointerAligned(HANDLE hProcess, void* p);
	std::vector<uintptr_t> FindAllInstances(HANDLE hProcess, uintptr_t VTable);
	std::vector<uintptr_t> FindReferences(uintptr_t startAddress, size_t length, uintptr_t scanValue);
	std::vector<uintptr_t> FindCodeReferences(uintptr_t startAddress, size_t length, uintptr_t scanValue);
}

