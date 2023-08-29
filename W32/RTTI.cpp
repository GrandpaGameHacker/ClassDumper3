#include "RTTI.h"
#include "../Util/Strings.h"
#include "../ClassDumper3.h"

RTTI::RTTI(FTargetProcess* InProcess, std::string InModuleName)
{
	Process = InProcess;
	Module = Process->ModuleMap.GetModule(InModuleName);
	ModuleName = InModuleName;
	
	if (IsRunning64Bits())
	{
		ModuleBase = (uintptr_t)Module->BaseAddress;
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
}

std::vector<std::shared_ptr<_Class>> RTTI::FindAll(const std::string& ClassName)
{
	std::vector<std::shared_ptr<_Class>> Classes;
	std::string LowerClassName = ClassName;
	std::transform(LowerClassName.begin(), LowerClassName.end(), LowerClassName.begin(), ::tolower);

	for (auto& Entry : NameClassMap)
	{
		std::string LowerEntryFirst = Entry.first;
		std::transform(LowerEntryFirst.begin(), LowerEntryFirst.end(), LowerEntryFirst.begin(), ::tolower);

		if (LowerEntryFirst.find(LowerClassName) != std::string::npos)
		{
			Classes.push_back(Entry.second);
		}
	}

	return Classes;
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

std::vector<uintptr_t> RTTI::ScanForCodeReferences(const std::shared_ptr<_Class>& CClass)
{
	if (!CClass) return std::vector<uintptr_t>();
	
	std::vector<std::future<FMemoryBlock>> Blocks = Process->AsyncGetReadableMemory(true);
	std::vector<FMemoryBlock> BlocksToFree;
	std::vector<uintptr_t> References;
	
	for (auto& Block : Blocks)
	{
		FMemoryBlock MemoryBlock = Block.get();
		if (MemoryBlock.Size == 0) continue;
		if (!MemoryBlock.Copy) continue;
		
		BlocksToFree.push_back(MemoryBlock);
		
		uintptr_t MemoryBlockCopy = reinterpret_cast<uintptr_t>(MemoryBlock.Copy);
		uintptr_t MemoryBlockAddress = reinterpret_cast<uintptr_t>(MemoryBlock.Address);
		
		for (uintptr_t i = MemoryBlockCopy; i < MemoryBlockCopy + MemoryBlock.Size; i++)
		{
			uintptr_t RealAddress = i - MemoryBlockCopy + MemoryBlockAddress;
			uintptr_t Candidate = *(DWORD*)i;
			Candidate += RealAddress + 4;
			if (Candidate == CClass->VTable)
			{
				// log reference
				ClassDumper3::LogF("Found reference to %s at 0x%llX", CClass->Name.c_str(), RealAddress);
				References.push_back(RealAddress);
			}
		}
	}

	for (FMemoryBlock& Block : BlocksToFree)
	{
		Block.FreeBlock();
	}
	
	CClass->CodeReferences = References;
	return References;
}

std::vector<uintptr_t> RTTI::ScanForClassInstances(const std::shared_ptr<_Class>& CClass)
{
	return std::vector<uintptr_t>();
}

void RTTI::FindValidSections()
{
	SetLoadingStage("Finding valid PE sections");
	bool bFoundExecutable = false;
	bool bFoundReadOnly = false;
	
	// find valid executable or read only sections
	for (auto& section : Module->Sections)
	{
		if (section.bFlagExecutable)
		{
			ExecutableSections.push_back(section);
			bFoundExecutable = true;
		}

		if (section.bFlagReadonly && !section.bFlagExecutable)
		{
			ReadOnlySections.push_back(section);
			bFoundReadOnly = true;
		}
	}

	if (!bFoundExecutable || !bFoundReadOnly)
	{
		ClassDumper3::Log("Failed to find valid sections for RTTI scan");
		SetLoadingStage("Error: Failed to find valid sections for RTTI scan");
	}
}

bool RTTI::IsInExecutableSection(uintptr_t Address)
{
	for (const FModuleSection& Section : ExecutableSections)
	{
		if (Section.Contains(Address))
		{
			return true;
		}
	}
	return false;
}

bool RTTI::IsInReadOnlySection(uintptr_t Address)
{
	for (const FModuleSection& Section : ReadOnlySections)
	{
		if (Section.Contains(Address))
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

	for (const FModuleSection& Section : ReadOnlySections)
	{
		TotalSectionSize += Section.Size();
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
		RTTITypeDescriptor TypeDescriptor;

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

		Process->Read(pTypeDescriptor, &TypeDescriptor, sizeof(RTTITypeDescriptor));

		if (!IsInReadOnlySection(TypeDescriptor.pVTable))
		{
			continue;
		}

		char Name[StandardBufferSize];
		Process->Read(pTypeDescriptor + offsetof(RTTITypeDescriptor, name), Name, StandardBufferSize);
		Name[StandardBufferSize - 1] = 0;
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
		RTTIClassHierarchyDescriptor ClassHierarchyDescriptor;
		
		Process->Read(PClassFinal.CompleteObjectLocator, &CompleteObjectLocator, sizeof(RTTICompleteObjectLocator));
		uintptr_t pClassDescriptor = CompleteObjectLocator.pClassDescriptor + ModuleBase;
		
		Process->Read(pClassDescriptor, &ClassHierarchyDescriptor, sizeof(RTTIClassHierarchyDescriptor));

		std::shared_ptr<_Class> ValidClass = std::make_shared<_Class>();
		ValidClass->CompleteObjectLocator = PClassFinal.CompleteObjectLocator;
		ValidClass->VTable = PClassFinal.VTable;
		ValidClass->MangledName = PClassFinal.Name;
		ValidClass->Name = PClassFinal.DemangledName;
		
		FilterSymbol(ValidClass->Name);

		ValidClass->VTableOffset = CompleteObjectLocator.offset;
		ValidClass->ConstructorDisplacementOffset = CompleteObjectLocator.cdOffset;
		ValidClass->numBaseClasses = ClassHierarchyDescriptor.numBaseClasses;

		ValidClass->bMultipleInheritance = (ClassHierarchyDescriptor.attributes >> 0) & 1;
		ValidClass->bVirtualInheritance = (ClassHierarchyDescriptor.attributes >> 1) & 1;
		ValidClass->bAmbigious = (ClassHierarchyDescriptor.attributes >> 2) & 1;

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
	
	for (std::shared_ptr<_Class>& CClass : Classes)
	{
		if (CClass->numBaseClasses > 1)
		{
			// read class array (skip the first one)
			std::unique_ptr<DWORD[]> baseClassArray = std::make_unique<DWORD[]>(0x4000);
			
			RTTICompleteObjectLocator CompleteObjectLocator;
			RTTIClassHierarchyDescriptor ClassHierarchyDescriptor;
			
			std::vector<uintptr_t> BaseClasses;
			BaseClasses.reserve(CClass->numBaseClasses);
		
			Process->Read(CClass->CompleteObjectLocator, &CompleteObjectLocator, sizeof(RTTICompleteObjectLocator));

			uintptr_t pClassDescriptor = CompleteObjectLocator.pClassDescriptor + ModuleBase;
			
			Process->Read(pClassDescriptor, &ClassHierarchyDescriptor, sizeof(RTTIClassHierarchyDescriptor));
			
			uintptr_t pBaseClassArray = ClassHierarchyDescriptor.pBaseClassArray + ModuleBase;
			Process->Read(pBaseClassArray, baseClassArray.get(), sizeof(uintptr_t) * CClass->numBaseClasses - 1);

			for (unsigned int i = 0; i < CClass->numBaseClasses - 1; i++)
			{
				BaseClasses.push_back(baseClassArray[i] + ModuleBase);
			}

			DWORD LastDisplacement = 0;
			DWORD Depth = 0;

			for (unsigned int i = 0; i < CClass->numBaseClasses - 1; i++)
			{
				RTTIBaseClassDescriptor BaseClassDescriptor;
				std::shared_ptr<_ParentClassNode> ParentClassNode = std::make_shared<_ParentClassNode>();
				Process->Read(BaseClasses[i], &BaseClassDescriptor, sizeof(RTTIBaseClassDescriptor));

				// process child name
				char name[StandardBufferSize];
				
				Process->Read((uintptr_t)BaseClassDescriptor.pTypeDescriptor + ModuleBase + offsetof(RTTITypeDescriptor, name), name, StandardBufferSize);
				name[StandardBufferSize - 1] = 0;
				
				ParentClassNode->MangledName = name;
				ParentClassNode->Name = DemangleMSVC(name);
				ParentClassNode->attributes = BaseClassDescriptor.attributes;
				FilterSymbol(ParentClassNode->Name);

				ParentClassNode->ChildClass = CClass;
				ParentClassNode->Class = FindFirst(ParentClassNode->Name);
				ParentClassNode->numContainedBases = BaseClassDescriptor.numContainedBases;
				ParentClassNode->where = BaseClassDescriptor.where;

				if (BaseClassDescriptor.where.mdisp == LastDisplacement)
				{
					Depth++;
				}
				else
				{
					LastDisplacement = BaseClassDescriptor.where.mdisp;
					Depth = 0;
				}
				ParentClassNode->TreeDepth = Depth;
				if (CClass->VTableOffset == ParentClassNode->where.mdisp && CClass->bInterface)
				{
					std::string OriginalName = CClass->Name;
					CClass->Name = ParentClassNode->Name;
					CClass->FormattedName = "interface " + OriginalName + " -> " + ParentClassNode->Name;
					CClass->MangledName = ParentClassNode->MangledName;
				}
				CClass->Parents.push_back(ParentClassNode);
			}
		}
	}
}

void RTTI::EnumerateVirtualFunctions(std::shared_ptr<_Class>& CClass)
{
	constexpr int MaximumVirtualFunctions = 0x4000;
	static std::unique_ptr<uintptr_t[]> buffer = std::make_unique<uintptr_t[]>(MaximumVirtualFunctions);
	
	memset(buffer.get(), 0, sizeof(uintptr_t) * MaximumVirtualFunctions);
	
	CClass->Functions.clear();
	
	Process->Read(CClass->VTable, buffer.get(), MaximumVirtualFunctions);
	
	for (size_t i = 0; i < MaximumVirtualFunctions / sizeof(uintptr_t); i++)
	{
		if (buffer[i] == 0)
		{
			break;
		}
		
		if (!IsInExecutableSection(buffer[i]))
		{
			break;
		}
		
		CClass->Functions.push_back(buffer[i]);
		
		std::string function_name = "sub_" + IntegerToHexStr(buffer[i]);
		CClass->FunctionNames.try_emplace(buffer[i], function_name);
	}
}

std::string RTTI::DemangleMSVC(char* Symbol)
{
	static const std::string VTABLE_SYMBOL_PREFIX = "??_7";
	static const std::string VTABLE_SYMBOL_SUFFIX = "6B@";
	char* pSymbol = nullptr;
	if (*static_cast<char*>(Symbol + 4) == '?') pSymbol = Symbol + 1;
	else if (*static_cast<char*>(Symbol) == '.') pSymbol = Symbol + 4;
	else if (*static_cast<char*>(Symbol) == '?') pSymbol = Symbol + 2;
	else
	{
		ClassDumper3::LogF("Unknown symbol format: %s", Symbol);
		return std::string(Symbol);
	}

	std::string ModifiedSymbol = pSymbol;
	ModifiedSymbol.insert(0, VTABLE_SYMBOL_PREFIX);
	ModifiedSymbol.insert(ModifiedSymbol.size(), VTABLE_SYMBOL_SUFFIX);
	char StringBuffer[StandardBufferSize];
	std::memset(StringBuffer, 0, StandardBufferSize);
	if (!UnDecorateSymbolName(ModifiedSymbol.c_str(), StringBuffer, StandardBufferSize, 0))
	{
		ClassDumper3::LogF("UnDecorateSymbolName failed: %s", Symbol);
		return std::string(Symbol);
	}

	return std::string(StringBuffer);
}

void RTTI::SortClasses()
{
	SetLoadingStage("Sorting classes...");
	std::sort(PotentialClassesFinal.begin(), PotentialClassesFinal.end(), [=](PotentialClass a, PotentialClass b)
		{
			return a.DemangledName < b.DemangledName;
		});
}

void RTTI::FilterSymbol(std::string& Symbol)
{
	static std::vector<std::string> Filters =
	{
		"::`vftable'",
		"const ",
		"::`anonymous namespace'"
	};

	for (auto& Filter : Filters)
	{
		size_t Pos;
		while ((Pos = Symbol.find(Filter)) != std::string::npos)
		{
			Symbol.erase(Pos, Filter.length());
		}
	}
}

void RTTI::SetLoadingStage(std::string Stage)
{
	std::scoped_lock Lock(LoadingStageMutex);
	LoadingStage = Stage;
}
