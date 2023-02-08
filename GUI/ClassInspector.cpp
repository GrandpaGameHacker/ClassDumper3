#include "ClassInspector.h"
#include "MainWindow.h"

ClassInspector::ClassInspector()
{
	MainWindow* main = IWindow::GetWindow<MainWindow>(EWindowType::MainWindow);
	if (main)
	{
		main->OnProcessSelected.BindObject(this, &ClassInspector::OnProcessSelectedDelegate);
		main->OnClassSelected.BindObject(this, &ClassInspector::OnClassSelectedDelegate);
	}
	else
	{
		std::cout << "ClassInspector : Main window does not exist!! " << std::endl;
		exit(-1);
	}
}

ClassInspector::~ClassInspector()
{
}

void ClassInspector::Draw()
{
	if (ImGui::Begin("Class Inspector", nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
	};
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
