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

MainWindow::~MainWindow()
{
}

void MainWindow::Draw()
{
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    ImVec2 windowSize(screenSize.x, screenSize.y * 0.75f);
    ImVec2 windowPos(0, 0);

    ImGui::SetNextWindowPos(windowPos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_FirstUseEver);

    ImGui::Begin(Title.c_str(), nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    if (ImGui::Button("Exit")) exit(0);

    ImGui::Text("Select a process to dump classes from:");
    ImGui::Separator();

    DrawProcessList();
	DrawModuleList();

    ImGui::Separator();
    ImGui::SameLine();

	if (ImGui::Button("Refresh/Filter"))
	{
		RefreshProcessList();
	}

    ImGui::SameLine();
	if (ImGui::Button("Scan RTTI"))
	{
		SelectProcess();
	}

    DrawClassList();

    ImGui::End();
}


void MainWindow::DrawProcessList()
{
	ImGui::InputText("Process filter...", &ProcessFilter);

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
	ProcessList = GetProcessList(ProcessFilter);
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
	auto SelectedClassLocked = SelectedClass.lock();
	if (!SelectedClassLocked) return;
	
	FilteredChildrenCache = RTTIObserver->FindChildClasses(SelectedClassLocked);
	ClassDumper3::LogF("Found %d children for %s", FilteredChildrenCache.size(), SelectedClassLocked->Name.c_str());
}

void MainWindow::DrawClassList()
{
	if (!RTTIObserver)
	{
		return;
	}

	if (RTTIObserver->IsAsyncProcessing())
	{
		ImGui::Text("Processing...");
		ImGui::Text("%s", RTTIObserver->GetProcessingStage().c_str());
		ImGui::Spinner("Spinner", 10, 10, 0xFF0000FF);
		return;
	}

	if (RTTIObserver->GetClasses().empty()) return;

	ImGui::Text("Class Filter:");

	if (ImGui::InputText("##", &ClassFilter, 0))
	{
		FilterClasses(ClassFilter);
	}
	else if (ClassFilter.empty() && !FilteredClassesCache.empty())
	{
		FilteredClassesCache.clear();
	}

	if (ImGui::Button("Scan For All References & Instances"))
	{
		RTTIObserver->ScanAllAsync();
	}

	if (RTTIObserver->IsAsyncScanning())
	{
		ImGui::SameLine();
		ImGui::Text("Scan in progress...");
		ImGui::SameLine();
		ImGui::Spinner("Spinner", 10, 10, 0xFF0000FF);
	}

	
	if (ImGui::Button("Filter Children"))
	{
		ClassFilter = "";
		FilteredClassesCache.clear();
		FilterChildren();
	}
	
	ImGui::SameLine();
	
	if (ImGui::Button("Clear Child Filter"))
	{
		FilteredChildrenCache.clear();
	}

	ImGui::BeginChildFrame(1, ImVec2(ImGui::GetWindowWidth(), ImGui::GetWindowHeight()));

	auto ClassesToDraw = RTTIObserver->GetClasses();
	
	if (!ClassFilter.empty() && !FilteredClassesCache.empty())
	{
		ClassesToDraw = FilteredClassesCache;
	}
	else if (!FilteredChildrenCache.empty())
	{
		ClassesToDraw = FilteredChildrenCache;
	}

	if (ClassesToDraw.empty())
	{
		ImGui::Text("No classes found for this filter");
	}

	for (const auto& cl : ClassesToDraw)
	{
		DrawClass(cl);
	}

	ImGui::EndChildFrame();
}

void MainWindow::DrawClass(const std::shared_ptr<ClassMetaData>& CMeta)
{
	auto SelectedClassLocked = SelectedClass.lock();
	bool bSelected = SelectedClassLocked && SelectedClassLocked->VTable == CMeta->VTable;
	
	if (bSelected) ImGui::PushStyleColor(ImGuiCol_Text, { 0, 255, 0, 255 });

	ImGui::Text(CMeta->FormattedName.c_str());

	if (bSelected) ImGui::PopStyleColor(1);

	if (ImGui::IsItemClicked(0))
	{
		OnClassSelected(CMeta);
		SelectedClass = CMeta;
	}
}

