#include "RTTI.h"
#include "../Util/Strings.h"
#include "../ClassDumper3.h"
#include "../Util/ThreadPool.h"
#include <numeric>
#include <DbgHelp.h>

RTTI::RTTI(FTargetProcess* InProcess, const std::string& InModuleName)
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
	std::vector<std::shared_ptr<ClassMetaData>> FoundClasses;
	std::string LowerClassName = ClassName;
	std::transform(LowerClassName.begin(), LowerClassName.end(), LowerClassName.begin(), ::tolower);

	for (auto& Entry : NameClassMap)
	{
		std::string LowerEntryFirst = Entry.first;
		std::transform(LowerEntryFirst.begin(), LowerEntryFirst.end(), LowerEntryFirst.begin(), ::tolower);

		if (LowerEntryFirst.find(LowerClassName) != std::string::npos)
		{
			FoundClasses.push_back(Entry.second);
		}
	}

	return FoundClasses;
}

std::vector<std::shared_ptr<ClassMetaData>> RTTI::FindChildClasses(const std::shared_ptr<ClassMetaData>& CMeta)
{
	std::vector<std::shared_ptr<ClassMetaData>> FoundClasses;

	for (auto& Entry : VTableClassMap)
	{
		if (Entry.second->IsChildOf(CMeta))
		{
			FoundClasses.push_back(Entry.second);
		}
	}

	return FoundClasses;
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

	if (!PotentialClasses.empty())
	{
		ValidateClasses(PotentialClasses);
	}

	bIsProcessing.store(false, std::memory_order_release);
}

void RTTI::ProcessRTTIAsync()
{
	if (bIsProcessing.load(std::memory_order_acquire))
	{
		return;
	}
	
	bIsProcessing.store(true, std::memory_order_release);
	ProcessThread = std::thread(&RTTI::ProcessRTTI, this);
	ProcessThread.detach();
}

const std::string& RTTI::GetProcessingStage()
{
	std::scoped_lock Lock(ProcessingStageMutex);
	ProcessingStageCache = ProcessingStage;
	return ProcessingStageCache;
}

std::vector<uintptr_t> RTTI::ScanMemory(const std::shared_ptr<ClassMetaData>& CMeta,
	std::vector<std::future<FMemoryBlock>>& Blocks,
	bool bInstanceScan)
{	std::vector<uintptr_t> Results;

	for (auto& Block : Blocks)
	{
		Block.wait();
		FMemoryBlock MemoryBlock = Block.get();

		auto MemoryBlockCopy = reinterpret_cast<uintptr_t>(MemoryBlock.Copy.data());
		auto MemoryBlockAddress = reinterpret_cast<uintptr_t>(MemoryBlock.Address);

		for (uintptr_t i = MemoryBlockCopy; i < MemoryBlockCopy + MemoryBlock.Size; i += (bInstanceScan ? 4 : 1))
		{
			uintptr_t Candidate = 0;
			uintptr_t RealAddress = i - MemoryBlockCopy + MemoryBlockAddress;

			if (bUse64BitScanner && !bInstanceScan)
			{
				Candidate = *reinterpret_cast<DWORD*>(i);
				Candidate += RealAddress + 4;
			}
			else
			{
				Candidate = *reinterpret_cast<uintptr_t*>(i);
			}

			if (Candidate % sizeof(void*) != 0) continue; // Skip unaligned addresses

			if (Candidate == CMeta->VTable)
			{
				const char* logMessage = bInstanceScan ? "Found %s Instance at 0x%p" : "Found reference to %s at 0x%p";
				ClassDumper3::LogF(logMessage, CMeta->Name.c_str(), RealAddress);
				Results.push_back(RealAddress);
			}
		}
	}

	return Results;
}

std::vector<uintptr_t> RTTI::ScanForCodeReferences(const std::shared_ptr<ClassMetaData>& CMeta)
{
	if (!CMeta)
	{
		return {};
	}

	std::vector<std::future<FMemoryBlock>> Blocks = Process->AsyncGetExecutableMemory();
	std::vector<uintptr_t> References = ScanMemory(CMeta, Blocks, false);
	CMeta->CodeReferences = References;
	bIsScanning.store(false, std::memory_order_release);
	return References;
}

std::vector<uintptr_t> RTTI::ScanForClassInstances(const std::shared_ptr<ClassMetaData>& CMeta)
{
	if (!CMeta)
	{
		return {};
	}

	std::vector<std::future<FMemoryBlock>> Blocks = Process->AsyncGetReadableMemory();
	std::vector<uintptr_t> Instances = ScanMemory(CMeta, Blocks, true);
	CMeta->ClassInstances = Instances;
	bIsScanning.store(false, std::memory_order_release);
	return Instances;
}

void RTTI::ProcessMemoryBlock(const FMemoryBlock& MemoryBlock, bool isForInstances, std::mutex& mtx)
{
	auto MemoryBlockCopy = reinterpret_cast<uintptr_t>(MemoryBlock.Copy.data());
	auto MemoryBlockAddress = reinterpret_cast<uintptr_t>(MemoryBlock.Address);

	for (uintptr_t i = MemoryBlockCopy; i < MemoryBlockCopy + MemoryBlock.Size; i += (isForInstances ? 4 : 1))
	{
		uintptr_t Candidate = 0;
		uintptr_t RealAddress = i - MemoryBlockCopy + MemoryBlockAddress;

		if (bUse64BitScanner && !isForInstances)
		{
			Candidate = *reinterpret_cast<DWORD*>(i);
			Candidate += RealAddress + 4;
		}
		else
		{
			Candidate = *reinterpret_cast<uintptr_t*>(i);
		}

		if (Candidate % sizeof(void*) != 0) continue; // Skip unaligned addresses

		auto it = VTableClassMap.find(Candidate);
		if (it != VTableClassMap.end())
		{
			const char* logMessage = isForInstances ? "Found %s instance at 0x%p" : "Found reference to %s at 0x%p";
			ClassDumper3::LogF(logMessage, it->second->Name.c_str(), RealAddress);

			std::scoped_lock Lock(mtx);
			if (isForInstances)
			{
				it->second->ClassInstances.push_back(RealAddress);
			}
			else
			{
				it->second->CodeReferences.push_back(RealAddress);
			}
		}
	}
}

void RTTI::ScanAllMemory(std::vector<std::future<FMemoryBlock>>& Blocks, bool isForInstances)
{
	std::mutex mtx;
	ThreadPool pool(std::thread::hardware_concurrency());

	for (auto& Block : Blocks)
	{
		pool.enqueue([&]() mutable
		{
				Block.wait();
				FMemoryBlock MemoryBlock = Block.get();
				if (!MemoryBlock.IsValid())
				{
					return;
				}

				ProcessMemoryBlock(MemoryBlock, isForInstances, mtx);
		});
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
	for (const std::shared_ptr<ClassMetaData>& Class : Classes)
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

void RTTI::ScanForCodeReferencesAsync(const std::shared_ptr<ClassMetaData>& CMeta)
{
	if (bIsScanning.load(std::memory_order_acquire)) return;

	bIsScanning.store(true, std::memory_order_release);
	ScannerThread = std::thread(&RTTI::ScanForCodeReferences, this, CMeta);
	ScannerThread.detach();
}

void RTTI::ScanForClassInstancesAsync(const std::shared_ptr<ClassMetaData>& CMeta)
{
	if (bIsScanning.load(std::memory_order_acquire)) return;

	bIsScanning.store(true, std::memory_order_release);
	ScannerThread = std::thread(&RTTI::ScanForClassInstances, this, CMeta);
	ScannerThread.detach();
}


void RTTI::FindValidSections()
{
	SetProcessingStage("Finding valid PE sections");
	bool bFoundExecutable = false;
	bool bFoundReadOnly = false;
	
	// find valid executable or read only sections
	for (auto& section : Module->Sections)
	{
		if (section.bFlagExecutable)
		{
			ExecutableSections.push_back(section);
			bFoundExecutable = true;
			continue;
		}

		if (section.bFlagReadonly)
		{
			ReadOnlySections.push_back(section);
			bFoundReadOnly = true;
		}
	}

	if (!bFoundExecutable || !bFoundReadOnly)
	{
		ClassDumper3::Log("Failed to find valid sections for RTTI scan");
		SetProcessingStage("Error: Failed to find valid sections for RTTI scan");
	}
}

bool RTTI::IsInExecutableSection(uintptr_t Address)
{	
	return std::any_of(ExecutableSections.begin(), ExecutableSections.end(), [&](const FModuleSection& Section) { return Section.Contains(Address); });
}

bool RTTI::IsInReadOnlySection(uintptr_t Address)
{
	return std::any_of(ReadOnlySections.begin(), ReadOnlySections.end(), [&](const FModuleSection& Section) { return Section.Contains(Address); });
}

void RTTI::ScanForClasses(std::vector<PotentialClass>& PotentialClasses)
{
	SetProcessingStage("Scanning for potential classes...");

	auto Accumulator = [](size_t sum, const FModuleSection& Section)
		{ return sum + Section.Size(); };

	size_t TotalSectionSize = std::accumulate(ReadOnlySections.begin(), ReadOnlySections.end(), size_t(0), Accumulator);

	auto SectionBuffer = std::vector<uintptr_t>(TotalSectionSize / sizeof(uintptr_t));

	for (const FModuleSection& Section : ReadOnlySections)
	{
		size_t SectionSize = Section.Size();
		size_t SectionMax = SectionSize / sizeof(uintptr_t);

		Process->Read(Section.Start, SectionBuffer.data(), SectionSize);

		for (size_t Index = 0; Index < SectionMax; Index++)
		{
			if (Index >= SectionBuffer.size() || Index + 1 >= SectionBuffer.size())
			{
				ClassDumper3::Log("Error: Index out of bounds in RTTI scan");
				break;
			}
			
			// if nullptr or we are at the end of the section
			if (SectionBuffer[Index] == 0 || Index + 1 > SectionMax)
			{
				continue;
			}

			// first pointer is not a valid RTTI object or the second pointer is not a valid VTable function ptr
			if (!IsInReadOnlySection(SectionBuffer[Index]) || !IsInExecutableSection(SectionBuffer[Index + 1]))
			{
				continue;
			}
			
			PotentialClass& PClass = PotentialClasses.emplace_back();
			PClass.CompleteObjectLocator = SectionBuffer[Index];
			// VTables are in order in MSVC so we can just add the index to the start of the section
			PClass.VTable = Section.Start + (Index + 1) * sizeof(uintptr_t);

		}
	}
	
	ClassDumper3::LogF("Found %u potential classes in %s\n", PotentialClasses.size(), ModuleName.c_str());
}


void RTTI::ValidateClasses(std::vector<PotentialClass>& PotentialClasses)
{
	SetProcessingStage("Validating potential classes...");
	
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

void RTTI::ProcessClasses(const std::vector<PotentialClass>& FinalClasses)
{
	SetProcessingStage("Processing class data...");

	std::string LastClassName = "";
	std::shared_ptr<ClassMetaData> LastClass = nullptr;

	for (const PotentialClass& PClassFinal : FinalClasses)
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
	SetProcessingStage("Processing parent class data...");
	
	for (const std::shared_ptr<ClassMetaData>& CMeta : Classes)
	{
		if (CMeta->numBaseClasses <= 1)
		{
			continue;
		}

		// read class array (skip the first one)
		std::unique_ptr<DWORD[]> baseClassArray = std::make_unique<DWORD[]>(0x4000);

		RTTICompleteObjectLocator CompleteObjectLocator;
		RTTIClassHierarchyDescriptor ClassHierarchyDescriptor;

		std::vector<uintptr_t> BaseClasses;
		BaseClasses.reserve(CMeta->numBaseClasses);

		Process->Read(CMeta->CompleteObjectLocator, &CompleteObjectLocator, sizeof(RTTICompleteObjectLocator));

		uintptr_t pClassDescriptor = CompleteObjectLocator.pClassDescriptor + ModuleBase;

		Process->Read(pClassDescriptor, &ClassHierarchyDescriptor, sizeof(RTTIClassHierarchyDescriptor));

		uintptr_t pBaseClassArray = ClassHierarchyDescriptor.pBaseClassArray + ModuleBase;
		Process->Read(pBaseClassArray, baseClassArray.get(), sizeof(uintptr_t) * CMeta->numBaseClasses - 1);

		for (unsigned int i = 0; i < CMeta->numBaseClasses - 1; ++i)
		{
			BaseClasses.emplace_back(baseClassArray[i] + ModuleBase);
		}

		DWORD LastDisplacement = 0;
		DWORD Depth = 0;

		for (unsigned int i = 0; i < CMeta->numBaseClasses - 1; i++)
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

			ParentClassNode->ChildClass = CMeta;
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

			if (CMeta->VTableOffset == ParentClassNode->where.mdisp && CMeta->bInterface)
			{
				std::string OriginalName = CMeta->Name;
				CMeta->Name = ParentClassNode->Name;
				CMeta->FormattedName = "interface " + OriginalName + " -> " + ParentClassNode->Name;
				CMeta->MangledName = ParentClassNode->MangledName;
			}
			CMeta->Parents.push_back(ParentClassNode);
		}
	}
}

void RTTI::EnumerateVirtualFunctions(const std::shared_ptr<ClassMetaData>& CMeta)
{
	constexpr int MaximumVirtualFunctions = 0x4000;
	static std::unique_ptr<uintptr_t[]> buffer = std::make_unique<uintptr_t[]>(MaximumVirtualFunctions);
	
	memset(buffer.get(), 0, sizeof(uintptr_t) * MaximumVirtualFunctions);
	
	CMeta->Functions.clear();
	
	Process->Read(CMeta->VTable, buffer.get(), MaximumVirtualFunctions);
	
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
		
		CMeta->Functions.push_back(buffer[i]);
		
		std::string function_name = "sub_" + IntegerToHexStr(buffer[i]);
		CMeta->FunctionNames.try_emplace(buffer[i], function_name);
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
	SetProcessingStage("Sorting classes...");
	std::sort(Classes.begin(), Classes.end(), [&](const PotentialClass& a, const PotentialClass& b){ return a.DemangledName < b.DemangledName; });
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

void RTTI::SetProcessingStage(const std::string& Stage)
{
	std::scoped_lock Lock(ProcessingStageMutex);
	ProcessingStage = Stage;
}

bool ClassMetaData::IsChildOf(const std::shared_ptr<ClassMetaData>& CMeta) const
{
	return std::any_of(Parents.begin(), Parents.end(),
		[&](const std::shared_ptr<ParentClass>& Parent)
		{ return Parent->Name == CMeta->Name; });
}
