#include "ClassInspector.h"
#include "MainWindow.h"
#include "../Util/Strings.h"
#include "imgui_stl.h"

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
		ImGui::Begin(WindowTitle, nullptr, ImGuiWindowFlags_NoCollapse);

		ImGui::Text("Name: %s", SelectedClass->Name.c_str());
		ImGui::Text("CompletObjectLocator: 0x%s", IntegerToHexStr(SelectedClass->CompleteObjectLocator).c_str());
		ImGui::Text("Inherited: %d", SelectedClass->Parents.size());
		{
			auto color = ScopedColor(ImGuiCol_Text, Color::Red);
			for (std::shared_ptr<_ParentClassNode> parent : SelectedClass->Parents)
			{
				ImGui::Text(parent->Name.c_str());
			}
		}

		ImGui::Text("Interfaces: %d", SelectedClass->Interfaces.size());
		{
			auto color = ScopedColor(ImGuiCol_Text, Color::Magenta);
			for (std::shared_ptr<_Class> inter : SelectedClass->Interfaces)
			{
				ImGui::Text(inter->Name.c_str());
			}
		}

		ImGui::Text("Virtual Function Table: 0x%s", IntegerToHexStr(SelectedClass->VTable).c_str());
		ImGui::Text("Virtual Functions: %d", SelectedClass->Functions.size());

		{
			auto color = ScopedColor(ImGuiCol_Text, Color::Green);
			for (auto& func : SelectedClass->FunctionNames)
			{
				std::string function_text = IntegerToHexStr(func.first) + " : " + func.second;
				ImGui::Text(function_text.c_str());
				if (ImGui::IsItemClicked(0))
				{
					// insert disassembler tool here
				}
				if (ImGui::IsItemClicked(1))
				{
					RenameFunction(&func);
				}
			}
		}
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

void ClassInspector::RenameFunction(std::pair<const uintptr_t, std::string>* func)
{
	if (RenamePopupWnd)
	{
		RenamePopupWnd->~RenamePopup();
		RenamePopupWnd.reset();
	}

	RenamePopupWnd = IWindow::Create<RenamePopup>();
	RenamePopupWnd->Initialize(func);
}

void RenamePopup::Initialize(std::pair<const uintptr_t, std::string>* func)
{
	func_ref = func;
	Enable();
}

void RenamePopup::Draw()
{
	ImGui::SetNextWindowFocus();

	ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);
	ImGui::Begin("Rename Function", nullptr, ImGuiWindowFlags_NoCollapse);

	ImGui::InputText("New name", &new_name, 0);
	if (ImGui::Button("Rename") && !new_name.empty())
	{
		func_ref->second = new_name;
		Disable();
	}

	if (ImGui::IsKeyDown(ImGuiKey_Enter) && !new_name.empty())
	{
		func_ref->second = new_name;
		Disable();
	}

	ImGui::SameLine();

	if (ImGui::Button("Cancel"))
	{
		Disable();
	}
	ImGui::End();
}
