#include "ClassInspector.h"
#include "MainWindow.h"
#include "../Util/Strings.h"
#include "imgui_stl.h"

ClassInspector::ClassInspector()
{
}

ClassInspector::~ClassInspector()
{
	std::shared_ptr<MainWindow> Main = IWindow::GetWindow<MainWindow>();
	if (Main)
	{
		Main->OnProcessSelected.DestroyBindings();
		Main->OnClassSelected.DestroyBindings();
	}
}

void ClassInspector::InitializeBindings()
{
	std::shared_ptr<MainWindow> Main = IWindow::GetWindow<MainWindow>();
	if (Main)
	{
		Main->OnProcessSelected.BindObject(shared_from_this(), &ClassInspector::OnProcessSelectedDelegate);
		Main->OnClassSelected.BindObject(shared_from_this(), &ClassInspector::OnClassSelectedDelegate);
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
		
		if (ImGui::Button("Copy To Clipboard"))
		{
			CopyInfo();
		}
		
		ImGui::Separator();
		ImGui::Text("Name: %s", SelectedClass->Name.c_str());

		ImGui::Text("CompleteObjectLocator: 0x%s", IntegerToHexStr(SelectedClass->CompleteObjectLocator).c_str());
		ImGui::Text("Num Inherited: %d", SelectedClass->Parents.size());
		{
			ScopedColor Color(ImGuiCol_Text, Color::Red);
			for (std::shared_ptr<_ParentClassNode> Parent : SelectedClass->Parents)
			{
				ImGui::Text(Parent->Name.c_str());
			}
		}

		ImGui::Text("Num Interfaces: %d", SelectedClass->Interfaces.size());
		{
			ScopedColor Color(ImGuiCol_Text, Color::Magenta);
			for (std::shared_ptr<_Class> Interface : SelectedClass->Interfaces)
			{
				ImGui::Text(Interface->Name.c_str());
			}
		}

		ImGui::Text("Virtual Function Table: 0x%s", IntegerToHexStr(SelectedClass->VTable).c_str());
		ImGui::Text("Num Virtual Functions: %d", SelectedClass->Functions.size());

		{
			ScopedColor Color(ImGuiCol_Text, Color::Green);
			int Index = 0;
			for (auto& Function: SelectedClass->FunctionNames)
			{
				
				std::string FunctionText = "%d - " + IntegerToHexStr(Function.first) + " : " + Function.second;
				ImGui::Text(FunctionText.c_str(), Index);
				if (ImGui::IsItemClicked(EMouseButton::Left))
				{
					// insert disassembler tool here
				}
				if (ImGui::IsItemClicked(EMouseButton::Right))
				{
					RenameFunction(&Function);
				}

				Index++;
			}
		}
		ImGui::End();
	}

}

void ClassInspector::OnProcessSelectedDelegate(std::shared_ptr<TargetProcess> InTarget, std::shared_ptr<RTTI> InRTTI)
{
	Target = InTarget;
	RTTIObserver = InRTTI;
}

void ClassInspector::OnClassSelectedDelegate(std::shared_ptr<_Class> InClass)
{
	SelectedClass = InClass;
	Enable();
}

void ClassInspector::RenameFunction(std::pair<const uintptr_t, std::string>* InFunction)
{
	if (RenamePopupWnd)
	{
		RenamePopupWnd->~RenamePopup();
		RenamePopupWnd.reset();
	}

	RenamePopupWnd = IWindow::Create<RenamePopup>();
	RenamePopupWnd->Initialize(InFunction);
}

void ClassInspector::CopyInfo()
{
    // Copy all class info
    std::string Info = "Name: " + SelectedClass->Name + "\n";
	Info += "CompleteObjectLocator: 0x" + IntegerToHexStr(SelectedClass->CompleteObjectLocator) + "\n";
	Info += "Num Inherited: " + std::to_string(SelectedClass->Parents.size()) + "\n";

    for (std::shared_ptr<_ParentClassNode> parent : SelectedClass->Parents)
    {
		Info += parent->Name + "\n";
    }

	Info += "Num Interfaces: " + std::to_string(SelectedClass->Interfaces.size()) + "\n";
	Info += "Virtual Function Table: 0x" + IntegerToHexStr(SelectedClass->VTable) + "\n";
	Info += "Num Virtual Functions: " + std::to_string(SelectedClass->Functions.size()) + "\n";

    for (auto& FunctionName : SelectedClass->FunctionNames)
    {
		Info += IntegerToHexStr(FunctionName.first) + " : " + FunctionName.second + "\n";
    }
    
    // Copy to clipboard
    if (OpenClipboard(nullptr))
    {
        EmptyClipboard();
        HGLOBAL ClipboardDataHandle = GlobalAlloc(GMEM_MOVEABLE, Info.size() + 1);

        if (ClipboardDataHandle != 0)
        {
            char* PCHData;
			PCHData = (char*)GlobalLock(ClipboardDataHandle);

            if (PCHData)
            {
                strcpy_s(PCHData, Info.size() + 1, Info.c_str());
                GlobalUnlock(ClipboardDataHandle);
                SetClipboardData(CF_TEXT, ClipboardDataHandle);
            }
        }

        CloseClipboard();
    }
}


void RenamePopup::Initialize(std::pair<const uintptr_t, std::string>* InFunction)
{
	FunctionPtr = InFunction;
	Enable();
}

void RenamePopup::Draw()
{
    ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(ImGui::GetMousePos(), ImGuiCond_Appearing);

    ImGui::Begin("Rename Function", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::InputText("New name", &NewName, 0);

    if (ImGui::Button("Rename") && !NewName.empty())
    {
        FunctionPtr->second = NewName;
        Disable();
    }

    if (ImGui::IsKeyDown(ImGuiKey_Enter) && !NewName.empty())
    {
        FunctionPtr->second = NewName;
        Disable();
    }

    ImGui::SameLine();

    if (ImGui::Button("Cancel"))
    {
        Disable();
    }

    ImGui::End();
}