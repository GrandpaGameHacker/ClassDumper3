#pragma once
#include "Memory.h"
#include <atomic>
#include <queue>

struct PMD
{
	int mdisp = 0; // member displacement
	int pdisp = 0; // vbtable displacement
	int vdisp = 0; // displacement inside vbtable
};

struct RTTIBaseClassDescriptor
{
	DWORD pTypeDescriptor = 0; // type descriptor of the class
	DWORD numContainedBases = 0; // number of nested classes in BaseClassArray
	PMD where; // pointer to member displacement info
	DWORD attributes = 0; // flags, usually 0
};

struct RTTIBaseClassArray
{
	// 0x4000 is the maximum number of inheritance allowed in some standards, but it will never exceed that lol ;)
	// Did this to avoid using C99 Variable Length Arrays, its not in the C++ standard
	DWORD arrayOfBaseClassDescriptors = 0; // describes base classes for the complete class
};

struct RTTIClassHierarchyDescriptor
{
	DWORD signature = 0; // 1 if 64 bit, 0 if 32bit
	DWORD attributes = 0; // bit 0 set = multiple inheritance, bit 1 set = virtual inheritance, bit 2 set = ambiguous
	DWORD numBaseClasses = 0; // number of classes in the BaseClassArray
	DWORD pBaseClassArray = 0; // array of base class descriptors
};

struct RTTICompleteObjectLocator
{
	DWORD signature = 0; // 1 if 64 bit, 0 if 32bit
	DWORD offset = 0; // offset of this vtable in the complete class
	DWORD cdOffset = 0; // constructor displacement offset
	DWORD pTypeDescriptor = 0; // type descriptor of the complete class
	DWORD pClassDescriptor = 0; // class descriptor for the complete class
};

struct RTTITypeDescriptor
{
	uintptr_t pVTable = 0; // pointer to the vftable
	uintptr_t spare = 0;
	char name = 0; // name of the class
};


struct PotentialClass
{
	uintptr_t CompleteObjectLocator = 0;
	uintptr_t VTable = 0;
	std::string Name;
	std::string DemangledName;
};

struct ParentClass;
struct ClassMetaData
{
	uintptr_t CompleteObjectLocator = 0;
	uintptr_t VTable = 0;

	std::string Name;
	std::string MangledName;
	std::string FormattedName;

	DWORD VTableOffset = 0;
	DWORD ConstructorDisplacementOffset = 0;

	std::vector<uintptr_t> Functions;
	std::map<uintptr_t, std::string> FunctionNames; // map of function names to addresses


	DWORD numBaseClasses = 0;
	std::vector<std::shared_ptr<ParentClass>> Parents;
	std::vector<std::weak_ptr<ClassMetaData>> Interfaces;
	std::vector<uintptr_t> CodeReferences;
	std::vector<uintptr_t> ClassInstances;

	bool bMultipleInheritance = false;
	bool bVirtualInheritance = false;
	bool bAmbigious = false;
	bool bStruct = false;
	bool bInterface = false;
};

struct ParentClass
{
	// basic class info
	std::string Name;
	std::string MangledName;
	DWORD numContainedBases = 0;
	PMD where = { 0,0,0 };
	DWORD attributes = 0;

	// lowest child class (the root of the tree)
	std::weak_ptr<ClassMetaData> ChildClass;
	
	// base class of this class (found by looking for class of the same name)
	std::weak_ptr<ClassMetaData> Class;
	
	// depth of the class in the tree
	DWORD TreeDepth = 0;
};

/************************************************************************/
/* RTTI Scanner class to get RTTI info from a process                   */
/* Results are in a queryable API									    */
/* Only supports MSVC virtual inheritance / symbol demangling			*/
/************************************************************************/

class RTTI
{
public:
	RTTI(FTargetProcess* InProcess, std::string InModuleName);
	std::shared_ptr<ClassMetaData> Find(uintptr_t VTable);
	std::shared_ptr<ClassMetaData> FindFirst(const std::string& ClassName);
	std::vector<std::shared_ptr<ClassMetaData>> FindAll(const std::string& ClassName);
	std::vector<std::shared_ptr<ClassMetaData>> GetClasses();

	void ProcessRTTI();
	
	void ProcessRTTIAsync();

	bool IsAsyncProcessing() const { return bIsProcessing.load(std::memory_order_acquire); }

	std::string GetProcessingStage();
	
	
	void ScanAllAsync();
	void ScanForCodeReferencesAsync(const std::shared_ptr<ClassMetaData>& CClass);
	void ScanForClassInstancesAsync(const std::shared_ptr<ClassMetaData>& CClass);
	inline bool IsAsyncScanning() const { return bIsScanning.load(std::memory_order_acquire); }

protected:
	void FindValidSections();
	bool IsInExecutableSection(uintptr_t Address);
	bool IsInReadOnlySection(uintptr_t Address);

	void SetProcessingStage(std::string Stage);

	void ScanForClasses(std::vector<PotentialClass>& PotentialClasses);
	void ValidateClasses(std::vector<PotentialClass>& PotentialClasses);
	void ProcessClasses(std::vector<PotentialClass>& FinalClasses);
	void ProcessParentClasses();

	// todo: name functions based on what class they are from...
	void EnumerateVirtualFunctions(std::shared_ptr<ClassMetaData>& c);

	std::string DemangleMSVC(char* Symbol);
	void SortClasses(std::vector<PotentialClass>& Classes);
	void FilterSymbol(std::string& Symbol);
	
	void ScanAllMemory(std::vector<std::future<FMemoryBlock>>& Blocks, bool isForInstances);
	void ProcessMemoryBlock(const FMemoryBlock& MemoryBlock, bool isForInstances, std::mutex& mtx);
	
	std::vector<uintptr_t> ScanMemory(
		const std::shared_ptr<ClassMetaData>& CClass,
		std::vector<std::future<FMemoryBlock>>& Blocks,
		bool isForInstances);

	std::vector<uintptr_t> ScanForCodeReferences(const std::shared_ptr<ClassMetaData>& CClass);
	std::vector<uintptr_t> ScanForClassInstances(const std::shared_ptr<ClassMetaData>& CClass);
	
	void ScanAll();
	void ScanForAllCodeReferences();
	void ScanForAllClassInstances();
	
	/************************************************************************/
	/*	RTTI Scanning */
	/************************************************************************/
	std::atomic_bool bIsProcessing = false;
	std::thread ProcessThread;
	constexpr static size_t LoadingStageSize = 128;
	std::mutex ProcessingStageMutex;
	std::string ProcessingStage = "Not Loading Anything...";
	std::string ProcessingStageCache;
	
	/************************************************************************/
	/*	CodeRef / Instance Scanning */
	/************************************************************************/

	std::atomic_bool bIsScanning = false;
	std::thread ScannerThread;
	bool bUse64BitScanner = sizeof(void*) == 8;

	/************************************************************************/
	/*	Process and Module Info
	/************************************************************************/
	
	FTargetProcess* Process;
	
	FModule* Module;
	std::string ModuleName;
	uintptr_t ModuleBase;
	std::vector<FModuleSection> ExecutableSections;
	std::vector<FModuleSection> ReadOnlySections;
	
	/************************************************************************/
	/*	Class Meta Data (Processed from RTTI and Memory Scans)
	/************************************************************************/
	
	std::vector<std::shared_ptr<ClassMetaData>> Classes;
	std::unordered_map<uintptr_t, std::shared_ptr<ClassMetaData>> VTableClassMap;
	std::unordered_map<std::string, std::shared_ptr<ClassMetaData>> NameClassMap;
};