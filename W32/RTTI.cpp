#include "RTTI.h"
#include "../Util/Strings.h"
#include "../ClassDumper3.h"

RTTI::RTTI(FTargetProcess* InProcess, std::string InModuleName)
{
	Process = InProcess;
	Module = Process->moduleMap.GetModule(InModuleName);
	ModuleName = InModuleName;
	
	if (IsRunning64Bits())
	{
		ModuleBase = (uintptr_t)Module->baseAddress;
	}
	else
	{
		// dont use the base address as 32 bits uses direct addresses instead of offsets
		ModuleBase = 0;
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
	for (auto& section : Module->sections)
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
	for (const FModuleSection& section : ExecutableSections)
	{
		if (address >= section.Start && address <= section.End)
		{
			return true;
		}
	}
	return false;
}

bool RTTI::IsInReadOnlySection(uintptr_t address)
{
	for (const FModuleSection& section : ReadOnlySections)
	{
		if (address >= section.Start && address <= section.End)
		{
			return true;
		}
	}
	return false;
}

void RTTI::ScanForClasses()
{
	SetLoadingStage("Scanning for potential classes...");
	
	uintptr_t* SectionBuffer;
	size_t TotalSectionSize = 0;

	for (const FModuleSection& section : ReadOnlySections)
	{
		TotalSectionSize += section.Size();
	}
	
	// now we only need one buffer alloc
	SectionBuffer = reinterpret_cast<uintptr_t*>(malloc(TotalSectionSize));

	if (!SectionBuffer)
	{
		std::cout << "Out of memory: line" << __LINE__;
		exit(-1);
	}
	
	memset(SectionBuffer, 0, TotalSectionSize);
	
	for (const FModuleSection& Section : ReadOnlySections)
	{
		size_t SectionSize = Section.Size();
		size_t Max = SectionSize / sizeof(uintptr_t);

		
		memset(SectionBuffer, 0, TotalSectionSize);
		Process->Read(Section.Start, SectionBuffer, SectionSize);

		
		for (size_t i = 0; i < Max; i++)
		{
			if (SectionBuffer[i] == 0 || i + 1 > Max)
			{
				continue;
			}

			if (IsInReadOnlySection(SectionBuffer[i]) && IsInExecutableSection(SectionBuffer[i + 1]))
			{
				PotentialClass PClass;
				PClass.CompleteObjectLocator = SectionBuffer[i];
				PClass.VTable = Section.Start + (i + 1) * (sizeof(uintptr_t));
				PotentialClasses.push_back(PClass);
			}
		}
	}
	
	free(SectionBuffer);
	ClassDumper3::LogF("Found %u potential classes in %s\n", PotentialClasses.size(), ModuleName.c_str()); 
	
	PotentialClassesFinal.reserve(PotentialClasses.size());
	Classes.reserve(PotentialClasses.size());
}

void RTTI::ValidateClasses()
{
	SetLoadingStage("Validating potential classes...");
	
	DWORD signatureMatch = IsRunning64Bits() ? 1 : 0;
	
	for (PotentialClass& PClass : PotentialClasses)
	{
		RTTICompleteObjectLocator CompleteObjectLocator;
		Process->Read(PClass.CompleteObjectLocator, &CompleteObjectLocator, sizeof(RTTICompleteObjectLocator));

		if (signatureMatch != CompleteObjectLocator.signature)
		{
			continue;
		}

		uintptr_t pTypeDescriptor = CompleteObjectLocator.pTypeDescriptor + ModuleBase;

		if (!IsInReadOnlySection(pTypeDescriptor))
		{
			continue;
		}

		RTTITypeDescriptor TypeDescriptor;
		Process->Read(pTypeDescriptor, &TypeDescriptor, sizeof(RTTITypeDescriptor));

		if (!IsInReadOnlySection(TypeDescriptor.pVTable))
		{
			continue;
		}

		char Name[bufferSize];
		Process->Read(pTypeDescriptor + offsetof(RTTITypeDescriptor, name), Name, bufferSize);
		Name[bufferSize - 1] = 0;
		PClass.Name = Name;
		PClass.DemangledName = DemangleMSVC(Name);

		PotentialClassesFinal.push_back(PClass);
	}

	PotentialClasses.clear();
	PotentialClasses.shrink_to_fit();
	
	SortClasses();
	ProcessClasses();

	ClassDumper3::LogF("Found %u valid classes in %s\n", Classes.size(), ModuleName.c_str());
}

void RTTI::ProcessClasses()
{
	SetLoadingStage("Processing class data...");
	
	std::string LastClassName = "";
	std::shared_ptr<_Class> LastClass = nullptr;
	
	for (PotentialClass& PClassFinal : PotentialClassesFinal)
	{
		RTTICompleteObjectLocator CompleteObjectLocator;
		Process->Read(PClassFinal.CompleteObjectLocator, &CompleteObjectLocator, sizeof(RTTICompleteObjectLocator));
		RTTIClassHierarchyDescriptor chd;

		uintptr_t pClassDescriptor = CompleteObjectLocator.pClassDescriptor + ModuleBase;
		Process->Read(pClassDescriptor, &chd, sizeof(RTTIClassHierarchyDescriptor));

		uintptr_t pTypeDescriptor = CompleteObjectLocator.pTypeDescriptor + ModuleBase;

		std::shared_ptr<_Class> ValidClass = std::make_shared<_Class>();
		ValidClass->CompleteObjectLocator = PClassFinal.CompleteObjectLocator;
		ValidClass->VTable = PClassFinal.VTable;
		ValidClass->MangledName = PClassFinal.Name;
		ValidClass->Name = PClassFinal.DemangledName;
		FilterSymbol(ValidClass->Name);

		ValidClass->VTableOffset = CompleteObjectLocator.offset;
		ValidClass->ConstructorDisplacementOffset = CompleteObjectLocator.cdOffset;
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
			Process->Read(c->CompleteObjectLocator, &col, sizeof(RTTICompleteObjectLocator));

			RTTIClassHierarchyDescriptor chd;
			uintptr_t pClassDescriptor = col.pClassDescriptor + ModuleBase;
			Process->Read(pClassDescriptor, &chd, sizeof(RTTIClassHierarchyDescriptor));
			uintptr_t pBaseClassArray = chd.pBaseClassArray + ModuleBase;
			Process->Read(pBaseClassArray, baseClassArray.get(), sizeof(uintptr_t) * c->numBaseClasses - 1);

			for (unsigned int i = 0; i < c->numBaseClasses - 1; i++)
			{
				baseClasses.push_back(baseClassArray[i] + ModuleBase);
			}

			DWORD lastdisplacement = 0;
			DWORD depth = 0;

			for (unsigned int i = 0; i < c->numBaseClasses - 1; i++)
			{
				RTTIBaseClassDescriptor bcd;
				std::shared_ptr<_ParentClassNode> node = std::make_shared<_ParentClassNode>();
				Process->Read(baseClasses[i], &bcd, sizeof(RTTIBaseClassDescriptor));

				// process child name
				char name[bufferSize];
				Process->Read((uintptr_t)bcd.pTypeDescriptor + ModuleBase + offsetof(RTTITypeDescriptor, name), name, bufferSize);
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
	
	Process->Read(c->VTable, buffer.get(), maxVFuncs);
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
