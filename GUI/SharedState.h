#pragma once
#include <Windows.h>
#include <imgui.h>
#include "..\flecs\flecs.h"
#include "..\Memory\Processes.h"

class SharedState
{
	SharedState() {};

	static flecs::world ecs;
	static flecs::entity App;

public:

	static bool bRunning;
	static bool bImportPopup, bExportPopup;
	static HANDLE hProcess;

	static char* szProcess;
	static PROCESSENTRY32* targetProcess;
	static std::vector<PROCESSENTRY32*> processList;

	static char* szModule;
	static MODULEENTRY32* targetModule;
	static std::vector<MODULEENTRY32*> moduleList;

	static std::vector<uintptr_t> VTables;
	static bool bSectionInfoGood;
	static bool bFoundVTables;

	static SectionInfo* targetSectionInfo;

	static void InitState();
	static void ApplicationLoop();
	static void CopyToClipboard(const char* format, ...);
};


typedef SharedState SS;
