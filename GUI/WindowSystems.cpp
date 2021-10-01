#include "WindowComponents.h"
#include "SharedState.h"

void MainWindowSystem(MainWindowComponent& window)
{
	if (ImGui::Begin(window.WindowID.c_str())) {
		ImGui::PushStyleColor(ImGuiCol_Button, window.DumpButtonColor);
		if (ImGui::Button("Dump!") && SS::targetProcess != nullptr && SS::targetModule != nullptr )
		{
			OnDumpButton(window);
		}
		ImGui::PopStyleColor();

		ImGui::PushStyleColor(ImGuiCol_Button, window.ImportButtonColor);
		ImGui::SameLine();
		if (ImGui::Button("Import")) {
			SS::bImportPopup = true;
		}
		ImGui::PopStyleColor();

		ImGui::PushStyleColor(ImGuiCol_Button, window.ExportButtonColor);
		ImGui::SameLine();
		if (ImGui::Button("Export")) {
			SS::bExportPopup = true;
		}
		ImGui::PopStyleColor();

		ImGui::PushStyleColor(ImGuiCol_Button, window.ExitButtonColor);
		ImGui::SameLine();
		if (ImGui::Button("Exit")) {
			SS::bRunning = false;
		}
		ImGui::PopStyleColor();

		ImGui::PushItemWidth(ImGui::GetWindowWidth() / 3);
		if (ImGui::BeginCombo("##ProcessCombo", SS::szProcess))
		{
			for (unsigned int n = 0; n < SS::processList.size(); n++)
			{
				bool is_selected = (SS::szProcess == SS::processList[n]->szExeFile);
				if (ImGui::Selectable(SS::processList[n]->szExeFile, is_selected))
				{
					SS::szProcess = SS::processList[n]->szExeFile;
					SS::targetProcess = SS::processList[n];
					SS::moduleList = GetModuleList(SS::targetProcess->th32ProcessID);
					SS::hProcess = OpenProcess(PROCESS_ALL_ACCESS, false, SS::targetProcess->th32ProcessID);
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		if (ImGui::Button("Refresh")) {
			SS::processList = GetProcessList();
		}

		if (ImGui::BeginCombo("##ModuleCombo", SS::szModule))
		{
			for (unsigned int n = 0; n < SS::moduleList.size(); n++)
			{
				bool is_selected = (SS::szModule == SS::moduleList[n]->szModule);
				if (ImGui::Selectable(SS::moduleList[n]->szModule, is_selected))
				{
					SS::szModule = SS::moduleList[n]->szModule;
					SS::targetModule = SS::moduleList[n];
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopItemWidth();

		ImGui::SetWindowPos(window.WindowPos, ImGuiCond_Once);
		ImGui::SetWindowSize(window.WindowSize, ImGuiCond_Once);
		ImGui::End();
	}
}

void OnDumpButton(MainWindowComponent& window)
{
	if (SS::hProcess == INVALID_HANDLE_VALUE || !IsProcessAlive(SS::hProcess))
	{
		return;
	}

	for (unsigned int i = 0; i < SS::VTables.size(); i++) {
		SS::VTables[i] = NULL;
	}

	SS::VTables.resize(0);
	SS::VTables.shrink_to_fit();
	SS::VTables.clear();

	SS::targetSectionInfo = GetSectionInfo(SS::hProcess, SS::targetModule);
	if (SS::targetSectionInfo) {
		SS::bSectionInfoGood = true;
		SS::VTables = FindAllVTables(SS::targetSectionInfo);
	}
}

void ClassViewerSystem(ClassViewerComponent& window)
{
	if (ImGui::Begin(window.WindowID.c_str())) {
		ImGui::SetWindowPos(window.WindowPos, ImGuiCond_Once);
		ImGui::SetWindowSize(window.WindowSize, ImGuiCond_Once);
		ImGui::End();
	}
}
