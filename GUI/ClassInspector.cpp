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
		if (ImGui::Begin("Class Inspector", nullptr, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::Text("Name: %s", SelectedClass->Name.c_str());
			ImGui::Text("CompletObjectLocator: 0x%llX", SelectedClass->CompleteObjectLocator);
			ImGui::Text("Virtual Function Table: 0x%llX", SelectedClass->VTable);
			ImGui::Text("Number of Virtual Functions: %d", SelectedClass->Functions.size());
			ImGui::Text("Number of Base Classes: %d", SelectedClass->Parents.size());
			ImGui::Text("Number of Interfaces: %d", SelectedClass->Interfaces.size());
			ImGui::End();
		};
	}

}

void ClassInspector::OnProcessSelectedDelegate(std::shared_ptr<TargetProcess> target, std::shared_ptr<RTTI> rtti)
{
	Target = target;
	RTTIObserver = rtti;
	std::cout << "Debug ClassInspector::OnProcessSelectedDelegate called" << std::endl;
}

void ClassInspector::OnClassSelectedDelegate(std::shared_ptr<_Class> cl)
{
	SelectedClass = cl;
	std::cout << "Debug ClassInspector::OnClassSelectedDelegate called" << std::endl;
}
