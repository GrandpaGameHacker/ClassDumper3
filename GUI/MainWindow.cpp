#include "MainWindow.h"
#include "imgui_stl.h"
#include <iostream>
#include "CustomWidgets.h"

MainWindow::MainWindow()
{
	processFilter = "";
	RefreshProcessList();
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

	
	ImGui::Begin("ClassDumper3", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus);
	if (ImGui::Button("Exit"))
	{
		exit(0);
	}
	ImGui::Text("Select a process to dump classes from:");
	ImGui::Separator();
	DrawProcessList();
	ImGui::Separator();
	ImGui::SameLine();
	if (ImGui::Button("Refresh/Filter"))
	{
		RefreshProcessList();
	}
	ImGui::SameLine();
	if (ImGui::Button("Select Process"))
	{
		SelectProcess();
	}

	DrawClassList();

	ImGui::End();
}

void MainWindow::DrawProcessList()
{
	ImGui::InputText("Process filter...", &processFilter);
	if (ImGui::BeginCombo("##ProcessCombo", selectedProcessName.c_str()))
	{
		for (unsigned int n = 0; n < ProcessList.size(); n++)
		{
			bool is_selected = (selectedProcessName == ProcessList[n].Name);
			if (ImGui::Selectable(ProcessList[n].Name.c_str(), is_selected))
			{
				Target = std::make_shared<FTargetProcess>();
				Target->Setup(ProcessList[n].PID);
				if (!Target->IsValid())
				{
					Target.reset();
					break;
				}
				selectedProcessName = ProcessList[n].Name;
			}
		}
		ImGui::EndCombo();
	}
}

void MainWindow::RefreshProcessList()
{
	if(processFilter.empty()) ProcessList = GetProcessList();
	else ProcessList = GetProcessList(processFilter);
}

void MainWindow::SelectProcess()
{
	if (RTTIObserver && RTTIObserver->IsAsyncProcessing()) return;
	if (Target && Target->IsValid())
	{
		RTTIObserver = std::make_shared<RTTI>(Target.get(), selectedProcessName);
		RTTIObserver->ProcessRTTIAsync();
		OnProcessSelected(Target, RTTIObserver);
	}
}

void MainWindow::FilterClasses(const std::string& filter)
{
	if (filter.empty()) return;
	if (!RTTIObserver) return;
	if (RTTIObserver->IsAsyncProcessing()) return;
	
	filteredClassesCache = RTTIObserver->FindAll(filter);
}

void MainWindow::DrawClassList()
{
	if(!RTTIObserver) return;
	if (RTTIObserver->IsAsyncProcessing())
	{
		ImGui::Text("Processing...");
		ImGui::Text("%s", RTTIObserver->GetLoadingStage().c_str());
		ImGui::Spinner("Spinner", 10, 10, 0xFF0000FF);
		return;
	}

	if(!RTTIObserver->GetClasses().size()) return;

	ImGui::Text("Class Filter:");

	if (ImGui::InputText("##", &classFilter, 0))
	{
		FilterClasses(classFilter);
	}
	else if (!classFilter.length() && filteredClassesCache.size())
	{
		filteredClassesCache.clear();
	}

	ImGui::BeginChildFrame(1, ImVec2(ImGui::GetWindowWidth() , ImGui::GetWindowHeight()));
	
	if (!classFilter.length())
	{
		for (const std::shared_ptr<_Class>& cl : RTTIObserver->GetClasses())
		{
			DrawClass(cl);
		}
	}
	else
	{
		if (!filteredClassesCache.size())
		{
			ImGui::Text("No classes found for this filter");
		}
		for (const std::shared_ptr<_Class>& cl : filteredClassesCache)
		{
			DrawClass(cl);
		}
	}

	ImGui::EndChildFrame();
}

void MainWindow::DrawClass(const std::shared_ptr<_Class>& cl)
{
	std::shared_ptr<_Class> SelectedClassLocked = SelectedClass.lock();

	if (SelectedClassLocked && SelectedClassLocked->VTable == cl->VTable)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, { 0, 255, 0, 255 });
		ImGui::Text(cl->FormattedName.c_str());
		ImGui::PopStyleColor(1);
		return;
	}
	
	ImGui::Text(cl->FormattedName.c_str());
	if (ImGui::IsItemClicked(0))
	{
		OnClassSelected(cl);
		SelectedClass = cl;
	}
	
}
