#include "RTTI.h"
#include "../Util/Strings.h"
#include "../ClassDumper3.h"

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
}

std::shared_ptr<_Class> RTTI::Find(uintptr_t VTable)
{
	auto it = VTableClassMap.find(VTable);
	if (it != VTableClassMap.end())
	{
		return it->second;
	}
	return nullptr;
}

std::shared_ptr<_Class> RTTI::FindFirst(const std::string& ClassName)
{
	auto it = NameClassMap.find(ClassName);
	if (it != NameClassMap.end())
	{
		return it->second;
	}
	
	return nullptr;
	
	// keeping this around just in case I fucked something up with optimizations
	//for (std::shared_ptr<_Class>& c : Classes)
	//{
	//	if (c->Name.find(ClassName) != std::string::npos)
	//	{
	//		return c;
	//	}
	//}
	//return nullptr;
}

std::vector<std::shared_ptr<_Class>> RTTI::FindAll(const std::string& ClassName)
{
	std::vector<std::shared_ptr<_Class>> classes;
	std::string lowerClassName = ClassName;
	std::transform(lowerClassName.begin(), lowerClassName.end(), lowerClassName.begin(), ::tolower);

	for (auto& entry : NameClassMap)
	{
		std::string lowerEntryFirst = entry.first;
		std::transform(lowerEntryFirst.begin(), lowerEntryFirst.end(), lowerEntryFirst.begin(), ::tolower);

		if (lowerEntryFirst.find(lowerClassName) != std::string::npos)
		{
			classes.push_back(entry.second);
		}
	}

	return classes;
}

std::vector<std::shared_ptr<_Class>> RTTI::GetClasses()
{
	return Classes;
}

void RTTI::ProcessRTTI()
{
	FindValidSections();

	ScanForClasses();

	if (PotentialClasses.size() > 0)
	{
		ValidateClasses();
	}

	bIsProcessing.store(false, std::memory_order_release);
}

void RTTI::ProcessRTTIAsync()
{
	if (bIsProcessing.load(std::memory_order_acquire)) return;
	
	bIsProcessing.store(true, std::memory_order_release);
	ProcessThread = std::thread(&RTTI::ProcessRTTI, this);
	ProcessThread.detach();
}

bool RTTI::IsAsyncProcessing()
{
	if (!bIsProcessing.load(std::memory_order_acquire))
	{
		return false;
	}

	return true;
}

std::string RTTI::GetLoadingStage()
{
	std::scoped_lock Lock(LoadingStageMutex);
	std::string LoadingStageCache = LoadingStage;
	return LoadingStageCache;
}

void RTTI::FindValidSections()
{
	SetLoadingStage("Finding valid PE sections");
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
		ClassDumper3::Log("Failed to find valid sections for RTTI scan");
		SetLoadingStage("Error: Failed to find valid sections for RTTI scan");
	}
}

bool RTTI::IsInExecutableSection(uintptr_t address)
{
	for (const ModuleSection& section : ExecutableSections)
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
	for (const ModuleSection& section : ReadOnlySections)
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
	SetLoadingStage("Scanning for classes...");
	uintptr_t* buffer;
	size_t sectionSize = 0;
	size_t max = 0;
	for (const ModuleSection& section : ReadOnlySections)
	{
		sectionSize = section.Size();
		size_t max = sectionSize / sizeof(uintptr_t);
		buffer = reinterpret_cast<uintptr_t*>(malloc(sectionSize));

		if (!buffer)
		{
			std::cout << "Out of memory: line" << __LINE__;
			exit(-1);
		}

		memset(buffer, 0, sectionSize);
		
		process->Read(section.start, buffer, sectionSize);

		
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
	
	ClassDumper3::LogF("Found %u potential classes in %s\n", PotentialClasses.size(), moduleName.c_str()); 
	
	PotentialClassesFinal.reserve(PotentialClasses.size());
	Classes.reserve(PotentialClasses.size());
}

void RTTI::ValidateClasses()
{
	SetLoadingStage("Validating classes...");
	bool bUse64bit = process->Is64Bit();
	for (PotentialClass& c : PotentialClasses)
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

		char Name[bufferSize];
		process->Read(pTypeDescriptor + offsetof(RTTITypeDescriptor, name), Name, bufferSize);
		c.DemangledName = DemangleMSVC(Name);

		PotentialClassesFinal.push_back(c);
	}

	PotentialClasses.clear();
	PotentialClasses.shrink_to_fit();
	SortClasses();
	ProcessClasses();

	ClassDumper3::LogF("Found %u valid classes in %s\n", Classes.size(), moduleName.c_str());
}

void RTTI::ProcessClasses()
{
	SetLoadingStage("Processing class data...");
	std::string LastClassName = "";
	std::shared_ptr<_Class> LastClass = nullptr;
	for (PotentialClass& c : PotentialClassesFinal)
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
		VTableClassMap.insert(std::pair<uintptr_t, std::shared_ptr<_Class>>(ValidClass->VTable, ValidClass));
		NameClassMap.insert(std::pair<std::string, std::shared_ptr<_Class>>(ValidClass->Name, ValidClass));
	}

	PotentialClassesFinal.clear();

	// process super classes
	SetLoadingStage("Processing super class data...");
	int interfaceCount = 0;
	for (std::shared_ptr<_Class>& c : Classes)
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

void RTTI::EnumerateVirtualFunctions(std::shared_ptr<_Class>& c)
{
	constexpr int maxVFuncs = 0x4000;
	static std::unique_ptr<uintptr_t[]> buffer = std::make_unique<uintptr_t[]>(maxVFuncs);
	
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
		ClassDumper3::LogF("Unknown symbol format: %s", symbol);
		return std::string(symbol);
	}

	std::string modifiedSymbol = pSymbol;
	modifiedSymbol.insert(0, VTABLE_SYMBOL_PREFIX);
	modifiedSymbol.insert(modifiedSymbol.size(), VTABLE_SYMBOL_SUFFIX);
	char buff[bufferSize];
	std::memset(buff, 0, bufferSize);
	if (!UnDecorateSymbolName(modifiedSymbol.c_str(), buff, bufferSize, 0))
	{
		ClassDumper3::LogF("UnDecorateSymbolName failed: %s", symbol);
		return std::string(symbol);
	}

	return std::string(buff);
}

void RTTI::SortClasses()
{
	SetLoadingStage("Sorting classes...");
	std::sort(PotentialClassesFinal.begin(), PotentialClassesFinal.end(), [=](PotentialClass a, PotentialClass b)
		{
			return a.DemangledName < b.DemangledName;
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

void RTTI::SetLoadingStage(std::string stage)
{
	std::scoped_lock Lock(LoadingStageMutex);
	LoadingStage = stage;
}
