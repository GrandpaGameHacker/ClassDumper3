#include "../Util/Strings.h"
#include "MainWindow.h"
#include "../ClassDumper3.h"
#include "imgui_stl.h"
#include <iostream>
#include "CustomWidgets.h"
MainWindow::MainWindow()
{
	ProcessFilter = "";
	RefreshProcessList();
	if (GetDebugPrivilege())
	{
		Title = "ClassDumper3 | SE_DEBUG_PRIVILEGE ON";
	}
}

void MainWindow::Draw()
{
	ImVec2 screenSize = ImGui::GetIO().DisplaySize;
	ImVec2 windowSize(screenSize.x, screenSize.y * 0.75f);
	ImVec2 windowPos(0, 0);

	ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);

	ImGui::Begin(Title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

	if (ImGui::Button("Exit"))
		exit(0);

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Process & Module Selection", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawProcessList();
		DrawModuleList();

		if (ImGui::Button("Scan RTTI"))
		{
			SelectProcess();
		}
	}

	if (ImGui::CollapsingHeader("Class Viewer", ImGuiTreeNodeFlags_DefaultOpen))
	{
		DrawClassList();
	}

	ImGui::End();
}


void MainWindow::DrawProcessList()
{
	ImGui::PushItemWidth(250);
	ImGui::InputText("Process Filter", &ProcessFilter);
	ImGui::PopItemWidth();

	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
	{
		RefreshProcessList();
	}
	if (!ImGui::BeginCombo("##ProcessCombo", SelectedProcessName.c_str()))
	{
		return;
	}

	for (const auto& Process : ProcessList)
	{
		const bool bIsSelected = (SelectedProcessName == Process.ProcessListName);

		if (ImGui::Selectable(Process.ProcessListName.c_str(), bIsSelected))
		{
			Target = std::make_shared<FTargetProcess>(Process.PID);

			if (!Target->IsValid())
			{
				ClassDumper3::LogF("Failed to attach to process %s", Process.ProcessListName.c_str());
				Target.reset();
				break;
			}

			ClassDumper3::LogF("Selected process %s", Process.ProcessListName.c_str());

			SelectedProcessName = Process.ProcessListName;
			SelectedModuleName = Target->ModuleMap.Modules[0].Name;
		}
	}
	ImGui::EndCombo();
}

void MainWindow::DrawModuleList()
{
	if (!Target) return;

	if (!ImGui::BeginCombo("##ModuleCombo", SelectedModuleName.c_str())) return;

	for (const auto& Module : Target->GetModules())
	{
		bool isSelected = (SelectedModuleName == Module.Name);

		if (ImGui::Selectable(Module.Name.c_str(), isSelected))
		{
			SelectedModuleName = Module.Name;
		}
	}
	ImGui::EndCombo();
}

void MainWindow::RefreshProcessList()
{
	std::string LowerFilter = ProcessFilter;
	StrLower(ProcessFilter);
	ProcessList = GetProcessList(LowerFilter);
}

void MainWindow::SelectProcess()
{
	if (RTTIObserver && RTTIObserver->IsAsyncProcessing()) return;
	if (Target && Target->IsValid())
	{
		RTTIObserver = std::make_shared<RTTI>(Target.get(), SelectedModuleName);
		RTTIObserver->ProcessRTTIAsync();
		OnProcessSelected(Target, RTTIObserver);
	}
}

void MainWindow::FilterClasses(const std::string& filter)
{
	if (filter.empty()) return;
	if (RTTIObserver->IsAsyncProcessing()) return;
	
	FilteredClassesCache = RTTIObserver->FindAll(filter);
}

void MainWindow::FilterChildren()
{
	auto SelectedClassLocked = SelectedClassWeak.lock();
	if (!SelectedClassLocked) return;
	
	FilteredChildrenCache = RTTIObserver->FindChildClasses(SelectedClassLocked);
	ClassDumper3::LogF("Found %d children for %s", FilteredChildrenCache.size(), SelectedClassLocked->Name.c_str());
}

void MainWindow::DrawClassList()
{
	if (!RTTIObserver) return;

	if (RTTIObserver->IsAsyncProcessing())
	{
		ImGui::Text("Processing...");
		ImGui::Text("%s", RTTIObserver->GetProcessingStage().c_str());
		ImGui::Spinner("ProcessingSpinner", 10, 10, 0xFF0000FF);
		return;
	}

	auto SelectedClass = SelectedClassWeak.lock();

	ImGui::Text("Class Filter:");
	ImGui::PushItemWidth(250);
	if (ImGui::InputText("##ClassFilter", &ClassFilter, 0))
	{
		FilterClasses(ClassFilter);
	}
	ImGui::PopItemWidth();

	ImGui::SameLine();
	if (ImGui::Button("Scan All References"))
	{
		RTTIObserver->ScanAllAsync();
	}

	if (RTTIObserver->IsAsyncScanning())
	{
		ImGui::SameLine();
		ImGui::Text("Scanning...");
		ImGui::SameLine();
		ImGui::Spinner("ScanSpinner", 10, 10, 0xFF0000FF);
	}

	if (ImGui::Button("Filter Children"))
	{
		ClassFilter.clear();
		FilteredClassesCache.clear();
		FilterChildren();
	}

	ImGui::SameLine();
	if (ImGui::Button("Clear Child Filter"))
	{
		FilteredChildrenCache.clear();
	}

	auto ClassesToDraw = RTTIObserver->GetClasses();

	if (!ClassFilter.empty() && !FilteredClassesCache.empty())
	{
		ClassesToDraw = FilteredClassesCache;
	}
	else if (!FilteredChildrenCache.empty())
	{
		ClassesToDraw = FilteredChildrenCache;
	}

	ImGui::BeginChild("ClassListFrame", ImVec2(0, 0), true);

	if (ClassesToDraw.empty())
	{
		ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "No classes found for this filter.");
	}
	else if (ImGui::BeginTable("ClassTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
	{
		ImGui::TableSetupColumn("Class Name");
		ImGui::TableSetupColumn("Functions");
		ImGui::TableSetupColumn("VTable");
		ImGui::TableHeadersRow();

		for (const auto& Class : ClassesToDraw)
		{
			const bool bIsSelected = SelectedClass && SelectedClass->VTable == Class->VTable;

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);

			// Determine colors
			if (bIsSelected)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 1, 0, 1));
			}
			else if (Class->bStruct)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1));
			}
			else if (Class->bInterface)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 1, 1));
			}

			ImGui::TextUnformatted(Class->Name.c_str());

			if (bIsSelected || Class->bStruct || Class->bInterface)
			{
				ImGui::PopStyleColor();
			}

			if (ImGui::IsItemClicked())
			{
				OnClassSelected(Class);
				SelectedClassWeak = Class;
			}

			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%d", Class->Functions.size());

			ImGui::TableSetColumnIndex(2);
			ImGui::Text("0x%p", reinterpret_cast<void*>(Class->VTable));
		}

		ImGui::EndTable();
	}

	ImGui::EndChild();
}

void MainWindow::DrawClass(const std::shared_ptr<ClassMetaData>& CMeta)
{
	auto SelectedClassLocked = SelectedClassWeak.lock();
	bool bSelected = SelectedClassLocked && SelectedClassLocked->VTable == CMeta->VTable;
	
	if (bSelected) ImGui::PushStyleColor(ImGuiCol_Text, { 0, 255, 0, 255 });

	ImGui::Text(CMeta->Name.c_str());

	if (bSelected) ImGui::PopStyleColor(1);

	if (ImGui::IsItemClicked(0))
	{
		OnClassSelected(CMeta);
		SelectedClassWeak = CMeta;
	}
}

