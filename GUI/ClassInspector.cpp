#include "ClassInspector.h"
#include "MainWindow.h"

ClassInspector::ClassInspector()
{
}

ClassInspector::~ClassInspector()
{
}

void ClassInspector::InitializeBindings()
{
	std::shared_ptr<MainWindow> main = IWindow::GetWindow<MainWindow>(EWindowType::MainWindow);
	if (main)
	{
		main->OnProcessSelected.BindObject(shared_from_this(), &ClassInspector::OnProcessSelectedDelegate);
		main->OnClassSelected.BindObject(shared_from_this(), &ClassInspector::OnClassSelectedDelegate);
	}
	else
	{
		std::cout << "ClassInspector : Main window does not exist!! " << std::endl;
		exit(-1);
	}
}

void ClassInspector::Draw()
{
	if (SelectedClass)
	{
		const char* WindowTitle = "Class Inspector###";
		
		if (SelectedClass->bInterface)
		{
			WindowTitle = "Interface Inspector###";
		}
		else if (SelectedClass->bStruct)
		{
			WindowTitle = "Structure Inspector###";
		}
		ImGui::Begin(WindowTitle, nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);
		
		ImGui::Text("Name: %s", SelectedClass->Name.c_str());
		ImGui::Text("CompletObjectLocator: 0x%llX", SelectedClass->CompleteObjectLocator);
		ImGui::Text("Virtual Function Table: 0x%llX", SelectedClass->VTable);
		ImGui::Text("Virtual Functions: %d", SelectedClass->Functions.size());
		ImGui::Text("Inherited: %d", SelectedClass->Parents.size());
		ImGui::Text("Interfaces: %d", SelectedClass->Interfaces.size());
		
		ImGui::End();
	}

}

void ClassInspector::OnProcessSelectedDelegate(std::shared_ptr<TargetProcess> target, std::shared_ptr<RTTI> rtti)
{
	Target = target;
	RTTIObserver = rtti;
}

void ClassInspector::OnClassSelectedDelegate(std::shared_ptr<_Class> cl)
{
	SelectedClass = cl;
	Enable();
}
