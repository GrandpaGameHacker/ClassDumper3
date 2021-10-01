#include "Memory.h"
const DWORD RWEMask = (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
	PAGE_EXECUTE_WRITECOPY);

const DWORD RWMask = (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY);
const DWORD BadPageMask = (PAGE_GUARD | PAGE_NOACCESS);

void* Memory::TranslateToRemote(void* localBase, void* local, void* remote)
{
    uintptr_t offset = (uintptr_t)local - (uintptr_t)localBase;
    return (void*)((uintptr_t)remote + offset);
}

bool Memory::IsBadReadPointer(HANDLE hProcess, void* p)
{
	MEMORY_BASIC_INFORMATION mbi = { nullptr };
	if (VirtualQueryEx(hProcess, p, &mbi, sizeof(mbi)))
	{
		bool b = !(mbi.Protect & RWEMask);
		if (mbi.Protect & BadPageMask) b = true;

		return b;
	}
	return true;
}

bool Memory::IsBadReadPointerAligned(HANDLE hProcess, void* p)
{
	if ((uintptr_t)p % sizeof(uintptr_t) != 0) {
		return true; // unaligned!
	}
	MEMORY_BASIC_INFORMATION mbi = { nullptr };
	if (VirtualQueryEx(hProcess, p, &mbi, sizeof(mbi)))
	{
		bool b = !(mbi.Protect & RWEMask);
		if (mbi.Protect & BadPageMask) b = true;

		return b;
	}
	return true;
}


// modified to reduce self references
std::vector<uintptr_t> Memory::FindReferences(uintptr_t startAddress, size_t length, uintptr_t scanValue)
{
	std::vector<uintptr_t> resultsList;
	uintptr_t newmem = (uintptr_t)malloc(length);
	if (!newmem) return resultsList;
	SIZE_T bytesRead = 0;
	if (!ReadProcessMemory((HANDLE)-1, (void*)startAddress, (void*)newmem, length, &bytesRead))
	{
		free((void*)newmem);
		return resultsList;
	}

	for (uintptr_t i = newmem; i < newmem + length; i += sizeof(uintptr_t))
	{
		uintptr_t candidate = *(uintptr_t*)i;
		if (candidate == (scanValue ^ 0xDEADBEEF))
		{
			uintptr_t realAddress = (i - newmem) + startAddress;
			resultsList.push_back(realAddress);
		}
	}
	memset((void*)newmem, 0, length);
	free((void*)newmem);
	return resultsList;
}

#ifdef _WIN64
std::vector<uintptr_t> Memory::FindCodeReferences(uintptr_t startAddress, size_t length, uintptr_t scanValue)
{
	std::vector<uintptr_t> resultsList;
	uintptr_t newmem = (uintptr_t)malloc(length);
	if (!newmem) return resultsList;
	SIZE_T bytesRead = 0;
	if (!ReadProcessMemory((HANDLE)-1, (void*)startAddress, (void*)newmem, length, &bytesRead))
	{
		free((void*)newmem);
		return resultsList;
	}

	for (uintptr_t i = newmem; i < newmem + length; i += 1)
	{
		uintptr_t realAddress = (i - newmem) + startAddress;
		uintptr_t candidate = *(DWORD*)i;
		candidate += realAddress + 4;
		if (candidate == (scanValue))
		{
			resultsList.push_back(realAddress);
		}
	}
	free((void*)newmem);
	return resultsList;
}
#else
std::vector<uintptr_t> Memory::FindCodeReferences(uintptr_t startAddress, size_t length, uintptr_t scanValue)
{
	std::vector<uintptr_t> resultsList;
	uintptr_t newmem = (uintptr_t)malloc(length);
	if (!newmem) return resultsList;
	SIZE_T bytesRead = 0;
	if (!ReadProcessMemory((HANDLE)-1, (void*)startAddress, (void*)newmem, length, &bytesRead))
	{
		free((void*)newmem);
		return resultsList;
	}

	for (uintptr_t i = newmem; i < newmem + length; i += 1)
	{
		uintptr_t candidate = *(uintptr_t*)i;
		if (candidate == (scanValue))
		{
			uintptr_t realAddress = (i - newmem) + startAddress;
			resultsList.push_back(realAddress);
		}
	}
	free((void*)newmem);
	return resultsList;
}
#endif
