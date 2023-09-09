#include "RTTI.h"
#include "../Util/Strings.h"
#include "../ClassDumper3.h"
#include "../Util/ThreadPool.h"

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

std::shared_ptr<ClassMetaData> RTTI::Find(uintptr_t VTable)
{
	auto it = VTableClassMap.find(VTable);
	if (it != VTableClassMap.end())
	{
		return it->second;
	}
	return nullptr;
}

std::shared_ptr<ClassMetaData> RTTI::FindFirst(const std::string& ClassName)
{
	auto it = NameClassMap.find(ClassName);
	if (it != NameClassMap.end())
	{
		return it->second;
	}
	
	return nullptr;
}

std::vector<std::shared_ptr<ClassMetaData>> RTTI::FindAll(const std::string& ClassName)
{
	std::vector<std::shared_ptr<ClassMetaData>> Classes;
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

std::vector<std::shared_ptr<ClassMetaData>> RTTI::GetClasses()
{
	return Classes;
}

void RTTI::ProcessRTTI()
{
	FindValidSections();
	
	std::vector<PotentialClass> PotentialClasses;
	
	ScanForClasses(PotentialClasses);

	if (PotentialClasses.size() > 0)
	{
		ValidateClasses(PotentialClasses);
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

std::string RTTI::GetLoadingStage()
{
	std::scoped_lock Lock(LoadingStageMutex);
	std::string LoadingStageCache = LoadingStage;
	return LoadingStageCache;
}

std::vector<uintptr_t> RTTI::ScanMemory(const std::shared_ptr<ClassMetaData>& CClass,
	std::vector<std::future<FMemoryBlock>>& Blocks,
	bool bInstanceScan)
{
	std::vector<FMemoryBlock> BlocksToFree;
	std::vector<uintptr_t> Results;

	for (auto& Block : Blocks)
	{
		Block.wait();
		FMemoryBlock MemoryBlock = Block.get();
		if (MemoryBlock.Size == 0) continue;
		if (!MemoryBlock.Copy) continue;

		BlocksToFree.push_back(MemoryBlock);

		uintptr_t MemoryBlockCopy = reinterpret_cast<uintptr_t>(MemoryBlock.Copy);
		uintptr_t MemoryBlockAddress = reinterpret_cast<uintptr_t>(MemoryBlock.Address);

		for (uintptr_t i = MemoryBlockCopy; i < MemoryBlockCopy + MemoryBlock.Size; i += (bInstanceScan ? 4 : 1))
		{
			uintptr_t Candidate = 0;
			uintptr_t RealAddress = i - MemoryBlockCopy + MemoryBlockAddress;

			if (bUse64BitScanner && !bInstanceScan)
			{
				Candidate = *(DWORD*)i;
				Candidate += RealAddress + 4;
			}
			else
			{
				Candidate = *(uintptr_t*)i;
			}

			if (Candidate % sizeof(void*) != 0) continue; // Skip unaligned addresses

			if (Candidate == CClass->VTable)
			{
				const char* logMessage = bInstanceScan ? "Found %s Instance at 0x%p" : "Found reference to %s at 0x%p";
				ClassDumper3::LogF(logMessage, CClass->Name.c_str(), RealAddress);
				Results.push_back(RealAddress);
			}
		}
	}

	for (FMemoryBlock& Block : BlocksToFree)
	{
		Block.FreeBlock();
	}

	return Results;
}

std::vector<uintptr_t> RTTI::ScanForCodeReferences(const std::shared_ptr<ClassMetaData>& CClass)
{
	if (!CClass) return std::vector<uintptr_t>();
	std::vector<std::future<FMemoryBlock>> Blocks = Process->AsyncGetExecutableMemory();
	std::vector<uintptr_t> References = ScanMemory(CClass, Blocks, false);
	CClass->CodeReferences = References;
	bIsScanning.store(false, std::memory_order_release);
	return References;
}

std::vector<uintptr_t> RTTI::ScanForClassInstances(const std::shared_ptr<ClassMetaData>& CClass)
{
	if (!CClass) return std::vector<uintptr_t>();
	std::vector<std::future<FMemoryBlock>> Blocks = Process->AsyncGetReadableMemory();
	std::vector<uintptr_t> Instances = ScanMemory(CClass, Blocks, true);
	CClass->ClassInstances = Instances;
	bIsScanning.store(false, std::memory_order_release);
	return Instances;
}

void RTTI::ProcessMemoryBlock(const FMemoryBlock& MemoryBlock, bool isForInstances, std::mutex& mtx)
{
	uintptr_t MemoryBlockCopy = reinterpret_cast<uintptr_t>(MemoryBlock.Copy);
	uintptr_t MemoryBlockAddress = reinterpret_cast<uintptr_t>(MemoryBlock.Address);

	for (uintptr_t i = MemoryBlockCopy; i < MemoryBlockCopy + MemoryBlock.Size; i += (isForInstances ? 4 : 1))
	{
		uintptr_t Candidate = 0;
		uintptr_t RealAddress = i - MemoryBlockCopy + MemoryBlockAddress;

		if (bUse64BitScanner && !isForInstances)
		{
			Candidate = *(DWORD*)i;
			Candidate += RealAddress + 4;
		}
		else
		{
			Candidate = *(uintptr_t*)i;
		}

		if (Candidate % sizeof(void*) != 0) continue; // Skip unaligned addresses

		auto it = VTableClassMap.find(Candidate);
		if (it != VTableClassMap.end())
		{
			const char* logMessage = isForInstances ? "Found %s instance at 0x%p" : "Found reference to %s at 0x%p";
			ClassDumper3::LogF(logMessage, it->second->Name.c_str(), RealAddress);

			std::scoped_lock Lock(mtx);
			if (isForInstances)
				it->second->ClassInstances.push_back(RealAddress);
			else
				it->second->CodeReferences.push_back(RealAddress);
		}
	}
}

void RTTI::ScanAllMemory(std::vector<std::future<FMemoryBlock>>& Blocks, bool isForInstances)
{
	std::vector<FMemoryBlock> BlocksToFree;
	{
		std::mutex mtx;

		ThreadPool pool(std::thread::hardware_concurrency());

		for (auto& Block : Blocks)
		{
			pool.enqueue([&]() mutable
			{
					Block.wait();
					FMemoryBlock MemoryBlock = Block.get();
					if (MemoryBlock.Size == 0) return;
					if (!MemoryBlock.Copy) return;

					{
						std::scoped_lock Lock(mtx);
						BlocksToFree.push_back(MemoryBlock);
					}

					ProcessMemoryBlock(MemoryBlock, isForInstances, mtx);
			});
		}
	}

	for (FMemoryBlock& Block : BlocksToFree)
	{
		Block.FreeBlock();
	}
}

void RTTI::ScanForAllCodeReferences()
{
	std::vector<std::future<FMemoryBlock>> Blocks = Process->AsyncGetExecutableMemory();
	ScanAllMemory(Blocks, false);
}

void RTTI::ScanForAllClassInstances()
{
	std::vector<std::future<FMemoryBlock>> Blocks = Process->AsyncGetReadableMemory();
	ScanAllMemory(Blocks, true);
}

void RTTI::ScanAll()
{
	for (std::shared_ptr<ClassMetaData>& Class : Classes)
	{
		Class->CodeReferences.clear();
		Class->ClassInstances.clear();
	}
	
	ScanForAllCodeReferences();
	ScanForAllClassInstances();
	
	bIsScanning.store(false, std::memory_order_release);
}

void RTTI::ScanAllAsync()
{
	if (bIsScanning.load(std::memory_order_acquire)) return;

	bIsScanning.store(true, std::memory_order_release);
	ScannerThread = std::thread(&RTTI::ScanAll, this);
	ScannerThread.detach();
}

void RTTI::ScanForCodeReferencesAsync(const std::shared_ptr<ClassMetaData>& CClass)
{
	if (bIsScanning.load(std::memory_order_acquire)) return;

	bIsScanning.store(true, std::memory_order_release);
	ScannerThread = std::thread(&RTTI::ScanForCodeReferences, this, CClass);
	ScannerThread.detach();
}

void RTTI::ScanForClassInstancesAsync(const std::shared_ptr<ClassMetaData>& CClass)
{
	if (bIsScanning.load(std::memory_order_acquire)) return;

	bIsScanning.store(true, std::memory_order_release);
	ScannerThread = std::thread(&RTTI::ScanForClassInstances, this, CClass);
	ScannerThread.detach();
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

void RTTI::ScanForClasses(std::vector<PotentialClass>& PotentialClasses)
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
}

void RTTI::ValidateClasses(std::vector<PotentialClass>& PotentialClasses)
{
	SetLoadingStage("Validating potential classes...");
	
	std::vector<PotentialClass> ValidatedClasses;
	ValidatedClasses.reserve(PotentialClasses.size());
	
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

		ValidatedClasses.push_back(PClass);
	}
	
	ProcessClasses(ValidatedClasses);
	
	ClassDumper3::LogF("Found %u valid classes in %s\n", Classes.size(), ModuleName.c_str());
}

void RTTI::ProcessClasses(std::vector<PotentialClass>& FinalClasses)
{
	SetLoadingStage("Processing class data...");

	std::string LastClassName = "";
	std::shared_ptr<ClassMetaData> LastClass = nullptr;

	for (PotentialClass& PClassFinal : FinalClasses)
	{
		RTTICompleteObjectLocator CompleteObjectLocator;
		RTTIClassHierarchyDescriptor ClassHierarchyDescriptor;

		Process->Read(PClassFinal.CompleteObjectLocator, &CompleteObjectLocator, sizeof(RTTICompleteObjectLocator));
		uintptr_t pClassDescriptor = CompleteObjectLocator.pClassDescriptor + ModuleBase;

		Process->Read(pClassDescriptor, &ClassHierarchyDescriptor, sizeof(RTTIClassHierarchyDescriptor));

		std::shared_ptr<ClassMetaData> ValidClass = std::make_shared<ClassMetaData>();
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
		VTableClassMap.insert(std::pair<uintptr_t, std::shared_ptr<ClassMetaData>>(ValidClass->VTable, ValidClass));
		NameClassMap.insert(std::pair<std::string, std::shared_ptr<ClassMetaData>>(ValidClass->Name, ValidClass));
	}
	
	ProcessParentClasses();
}

void RTTI::ProcessParentClasses()
{
	// process parent classes
	SetLoadingStage("Processing parent class data...");

	int interfaceCount = 0;

	for (std::shared_ptr<ClassMetaData>& CClass : Classes)
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
				std::shared_ptr<ParentClass> ParentClassNode = std::make_shared<ParentClass>();
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

void RTTI::EnumerateVirtualFunctions(std::shared_ptr<ClassMetaData>& CClass)
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

void RTTI::SortClasses(std::vector<PotentialClass>& Classes)
{
	SetLoadingStage("Sorting classes...");
	std::sort(Classes.begin(), Classes.end(), [=](PotentialClass a, PotentialClass b){ return a.DemangledName < b.DemangledName; });
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
