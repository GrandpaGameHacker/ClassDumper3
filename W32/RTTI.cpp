#include "RTTI.h"
#include "../Util/Strings.h"
RTTI::RTTI(TargetProcess* process, std::string moduleName)
{
	this->process = process;
	module = process->moduleMap.GetModule(moduleName);
	this->moduleName = moduleName;

	if (process->Is64Bit())
	{
		moduleBase = (uintptr_t)module->baseAddress;
	}
	else
	{
		// dont use the base address as 32 bits uses direct addresses instead of offsets
		moduleBase = 0;
	}

	FindValidSections();
	ScanForClasses();
	if (PotentialClasses.size() > 0)
	{
		ValidateClasses();
	}
}

std::shared_ptr<_Class> RTTI::Find(uintptr_t VTable)
{
	auto it = ClassMap.find(VTable);
	if (it != ClassMap.end())
	{
		return it->second;
	}
	return nullptr;
}

std::shared_ptr<_Class> RTTI::FindFirst(std::string ClassName)
{
	for (std::shared_ptr<_Class> c : Classes)
	{
		if (c->Name.find(ClassName) != std::string::npos)
		{
			return c;
		}
	}
	return nullptr;
}

std::vector<std::shared_ptr<_Class>> RTTI::FindAll(std::string ClassName)
{
	std::vector<std::shared_ptr<_Class>> classes;
	for (std::shared_ptr<_Class> c : Classes)
	{
		if (c->Name.find(ClassName) != std::string::npos)
		{
			classes.push_back(c);
		}
	}
	return classes;
}

std::vector<std::shared_ptr<_Class>> RTTI::GetClasses()
{
	return Classes;
}

void RTTI::FindValidSections()
{
	bool bFound1 = false;
	bool bFound2 = false;
	// find valid executable or read only sections
	for (auto& section : module->sections)
	{
		if (section.bFlagExecutable)
		{
			ExecutableSections.push_back(section);
			bFound1 = true;
		}

		if (section.bFlagReadonly && !section.bFlagExecutable)
		{
			ReadOnlySections.push_back(section);
			bFound2 = true;
		}
	}

	if (!bFound1 || !bFound2)
	{
		std::cout << "Failed to find valid sections for RTTI scan" << std::endl;
	}
}

bool RTTI::IsInExecutableSection(uintptr_t address)
{
	for (auto& section : ExecutableSections)
	{
		if (address >= section.start && address <= section.end)
		{
			return true;
		}
	}
	return false;
}

bool RTTI::IsInReadOnlySection(uintptr_t address)
{
	for (auto& section : ReadOnlySections)
	{
		if (address >= section.start && address <= section.end)
		{
			return true;
		}
	}
	return false;
}

void RTTI::ScanForClasses()
{
	uintptr_t* buffer;
	for (auto& section : ReadOnlySections)
	{
		buffer = (uintptr_t*)malloc(section.Size());
		if (buffer == nullptr)
		{
			std::cout << "Out of memory: line" << __LINE__;
			exit(0);
		}
		process->Read(section.start, buffer, section.Size());
		uintptr_t max = section.Size() / sizeof(uintptr_t);
		for (size_t i = 0; i < max; i++)
		{
			if (buffer[i] == 0 || i + 1 > max)
			{
				continue;
			}

			if (IsInReadOnlySection(buffer[i]) && IsInExecutableSection(buffer[i + 1]))
			{
				PotentialClass c;
				c.CompleteObjectLocator = buffer[i];
				c.VTable = section.start + (i + 1) * (sizeof(uintptr_t));
				PotentialClasses.push_back(c);
			}
		}
		free(buffer);
	}
	std::cout << "Found " << PotentialClasses.size() << " potential classes in " << moduleName << std::endl;
	PotentialClassesFinal.reserve(PotentialClasses.size());
	Classes.reserve(PotentialClasses.size());
}

void RTTI::ValidateClasses()
{
	bool bUse64bit = process->Is64Bit();
	for (PotentialClass c : PotentialClasses)
	{
		RTTICompleteObjectLocator col;
		process->Read(c.CompleteObjectLocator, &col, sizeof(RTTICompleteObjectLocator));

		if (bUse64bit)
		{
			if (col.signature != 1)
			{
				continue;
			}
		}
		else
		{
			if (col.signature != 0)
			{
				continue;
			}
		}

		uintptr_t pTypeDescriptor = col.pTypeDescriptor + moduleBase;

		if (!IsInReadOnlySection(pTypeDescriptor))
		{
			continue;
		}

		RTTITypeDescriptor td;
		process->Read(pTypeDescriptor, &td, sizeof(RTTITypeDescriptor));

		if (!IsInReadOnlySection(td.pVFTable))
		{
			continue;
		}

		PotentialClassesFinal.push_back(c);
	}

	PotentialClasses.clear();
	PotentialClasses.shrink_to_fit();
	SortClasses();
	ProcessClasses();


	std::cout << "Found " << Classes.size() << " valid classes in " << moduleName << std::endl;
}

void RTTI::ProcessClasses()
{
	std::string LastClassName = "";
	std::shared_ptr<_Class> LastClass = nullptr;
	for (PotentialClass c : PotentialClassesFinal)
	{
		RTTICompleteObjectLocator col;
		process->Read(c.CompleteObjectLocator, &col, sizeof(RTTICompleteObjectLocator));
		RTTIClassHierarchyDescriptor chd;

		uintptr_t pClassDescriptor = col.pClassDescriptor + moduleBase;
		process->Read(pClassDescriptor, &chd, sizeof(RTTIClassHierarchyDescriptor));

		uintptr_t pTypeDescriptor = col.pTypeDescriptor + moduleBase;

		std::shared_ptr<_Class> ValidClass = std::make_shared<_Class>();
		ValidClass->CompleteObjectLocator = c.CompleteObjectLocator;
		ValidClass->VTable = c.VTable;
		char name[bufferSize];
		memset(name, 0, bufferSize);
		process->Read(pTypeDescriptor + offsetof(RTTITypeDescriptor, name), name, bufferSize);
		name[bufferSize - 1] = 0;
		ValidClass->MangledName = name;
		ValidClass->Name = DemangleMSVC(name);
		FilterSymbol(ValidClass->Name);


		ValidClass->VTableOffset = col.offset;
		ValidClass->ConstructorDisplacementOffset = col.cdOffset;
		ValidClass->numBaseClasses = chd.numBaseClasses;

		ValidClass->bMultipleInheritance = (chd.attributes >> 0) & 1;
		ValidClass->bVirtualInheritance = (chd.attributes >> 1) & 1;
		ValidClass->bAmbigious = (chd.attributes >> 2) & 1;

		if (ValidClass->MangledName[3] == 'U')
		{
			ValidClass->bStruct = true;
		}

		if (ValidClass->Name == LastClassName && ValidClass->bMultipleInheritance)
		{
			ValidClass->bInterface = true;
			LastClass->Interfaces.push_back(ValidClass);
		}
		else
		{
			LastClassName = ValidClass->Name;
			LastClass = ValidClass;
		}

		if (ValidClass->bInterface)
		{
			ValidClass->FormattedName = "interface " + ValidClass->Name;
		}
		else if (ValidClass->bStruct)
		{
			ValidClass->FormattedName = "struct " + ValidClass->Name;
		}
		else
		{
			ValidClass->FormattedName = "class " + ValidClass->Name;
		}

		EnumerateVirtualFunctions(ValidClass);
		Classes.push_back(ValidClass);
		ClassMap.insert(std::pair<uintptr_t, std::shared_ptr<_Class>>(ValidClass->VTable, ValidClass));
	}
	PotentialClassesFinal.clear();
	PotentialClassesFinal.shrink_to_fit();

	// process super classes
	int interfaceCount = 0;
	for (std::shared_ptr<_Class> c : Classes)
	{
		if (c->numBaseClasses > 1)
		{
			// read class array (skip the first one)
			std::unique_ptr<DWORD[]> baseClassArray = std::make_unique<DWORD[]>(0x4000);
			std::vector<uintptr_t> baseClasses;
			baseClasses.reserve(c->numBaseClasses);
			RTTICompleteObjectLocator col;
			process->Read(c->CompleteObjectLocator, &col, sizeof(RTTICompleteObjectLocator));

			RTTIClassHierarchyDescriptor chd;
			uintptr_t pClassDescriptor = col.pClassDescriptor + moduleBase;
			process->Read(pClassDescriptor, &chd, sizeof(RTTIClassHierarchyDescriptor));
			uintptr_t pBaseClassArray = chd.pBaseClassArray + moduleBase;
			process->Read(pBaseClassArray, baseClassArray.get(), sizeof(uintptr_t) * c->numBaseClasses - 1);

			for (unsigned int i = 0; i < c->numBaseClasses - 1; i++)
			{
				baseClasses.push_back(baseClassArray[i] + moduleBase);
			}

			DWORD lastdisplacement = 0;
			DWORD depth = 0;

			for (unsigned int i = 0; i < c->numBaseClasses - 1; i++)
			{
				RTTIBaseClassDescriptor bcd;
				std::shared_ptr<_ParentClassNode> node = std::make_shared<_ParentClassNode>();
				process->Read(baseClasses[i], &bcd, sizeof(RTTIBaseClassDescriptor));

				// process child name
				char name[bufferSize];
				process->Read((uintptr_t)bcd.pTypeDescriptor + moduleBase + offsetof(RTTITypeDescriptor, name), name, bufferSize);
				name[bufferSize - 1] = 0;
				node->MangledName = name;
				node->Name = DemangleMSVC(name);
				node->attributes = bcd.attributes;
				FilterSymbol(node->Name);

				node->ChildClass = c;
				node->Class = FindFirst(node->Name);
				node->numContainedBases = bcd.numContainedBases;
				node->where = bcd.where;

				if (bcd.where.mdisp == lastdisplacement)
				{
					depth++;
				}
				else
				{
					lastdisplacement = bcd.where.mdisp;
					depth = 0;
				}
				node->treeDepth = depth;
				if (c->VTableOffset == node->where.mdisp && c->bInterface)
				{
					std::string OriginalName = c->Name;
					c->Name = node->Name;
					c->FormattedName = "interface " + OriginalName + " -> " + node->Name;
					c->MangledName = node->MangledName;
				}
				c->Parents.push_back(node);
			}
		}
	}
}

void RTTI::EnumerateVirtualFunctions(std::shared_ptr<_Class> c)
{
	constexpr int maxVFuncs = 0x4000;
	auto buffer = std::make_unique<uintptr_t[]>(maxVFuncs);
	memset(buffer.get(), 0, sizeof(uintptr_t) * maxVFuncs);
	c->Functions.clear();
	process->Read(c->VTable, buffer.get(), maxVFuncs);
	for (size_t i = 0; i < maxVFuncs / sizeof(uintptr_t); i++)
	{
		if (buffer[i] == 0)
		{
			break;
		}
		if (!IsInExecutableSection(buffer[i]))
		{
			break;
		}
		c->Functions.push_back(buffer[i]);
		std::string function_name = "sub_" + IntegerToHexStr(buffer[i]);
		c->FunctionNames.try_emplace(buffer[i], function_name);
	}
}

std::string RTTI::DemangleMSVC(char* symbol)
{
	static const std::string VTABLE_SYMBOL_PREFIX = "??_7";
	static const std::string VTABLE_SYMBOL_SUFFIX = "6B@";
	char* pSymbol = nullptr;
	if (*static_cast<char*>(symbol + 4) == '?') pSymbol = symbol + 1;
	else if (*static_cast<char*>(symbol) == '.') pSymbol = symbol + 4;
	else if (*static_cast<char*>(symbol) == '?') pSymbol = symbol + 2;
	else
	{
		std::cout << "Unknown symbol format: " << symbol << std::endl;
		return std::string(symbol);
	}

	std::string modifiedSymbol = pSymbol;
	modifiedSymbol.insert(0, VTABLE_SYMBOL_PREFIX);
	modifiedSymbol.insert(modifiedSymbol.size(), VTABLE_SYMBOL_SUFFIX);
	char buff[bufferSize];
	std::memset(buff, 0, bufferSize);
	if (!UnDecorateSymbolName(modifiedSymbol.c_str(), buff, bufferSize, 0))
	{
		std::cout << "UnDecorateSymbolName failed: " << GetLastError() << std::endl;
		return std::string(symbol);
	}

	return std::string(buff);
}

void RTTI::SortClasses()
{
	std::sort(PotentialClassesFinal.begin(), PotentialClassesFinal.end(), [=](PotentialClass a, PotentialClass b)
		{
			char aName[bufferSize];
			char bName[bufferSize];
			RTTICompleteObjectLocator col1, col2;
			process->Read(a.CompleteObjectLocator, &col1, sizeof(RTTICompleteObjectLocator));
			process->Read(b.CompleteObjectLocator, &col2, sizeof(RTTICompleteObjectLocator));
			uintptr_t pTypeDescriptor1 = (uintptr_t)col1.pTypeDescriptor + moduleBase;
			uintptr_t pTypeDescriptor2 = (uintptr_t)col2.pTypeDescriptor + moduleBase;
			process->Read(pTypeDescriptor1 + offsetof(RTTITypeDescriptor, name), aName, bufferSize);
			process->Read(pTypeDescriptor2 + offsetof(RTTITypeDescriptor, name), bName, bufferSize);
			std::string aNameStr = DemangleMSVC(aName);
			std::string bNameStr = DemangleMSVC(bName);
			return aNameStr < bNameStr;
		});
}

void RTTI::FilterSymbol(std::string& symbol)
{
	static std::vector<std::string> filters =
	{
		"::`vftable'",
		"const ",
		"::`anonymous namespace'"
	};

	for (auto& filter : filters)
	{
		size_t pos;
		while ((pos = symbol.find(filter)) != std::string::npos)
		{
			symbol.erase(pos, filter.length());
		}
	}
}
