#pragma once
#include <imgui.h>
#include <string>
struct MainWindowComponent
{
	std::string WindowID = "ClassDumper 3";
	ImVec2 WindowSize = ImVec2{ 900, 100 };;
	ImVec2 WindowPos = ImVec2{ 0,0 };;

	ImVec4 DumpButtonColor = ImVec4(ImColor(155, 0, 0));
	ImVec4 ImportButtonColor = ImVec4(ImColor(0, 155, 125));
	ImVec4 ExportButtonColor = ImVec4(ImColor(0, 155, 0));
	ImVec4 ExitButtonColor = ImVec4(ImColor(0, 0, 155));
};

void MainWindowSystem(MainWindowComponent& window);
void OnDumpButton(MainWindowComponent& window);


struct ClassViewerComponent {
	std::string WindowID = "ClassViewer";
	std::string fmt_Struct = "struct %s";
	std::string fmt_Interface = "\tinterface : %s";
	std::string fmt_Class = "class %s";

	ImVec2 WindowPos = ImVec2{ 0,101 };
	ImVec2 WindowSize = ImVec2{ 900,975 };
};

void ClassViewerSystem(ClassViewerComponent& window);