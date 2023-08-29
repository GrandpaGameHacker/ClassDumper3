
#include "ClassInspector.h"
#include "MainWindow.h"
#include "../Util/Strings.h"
#include "imgui_stl.h"
#include "../ClassDumper3.h"
#include "CustomWidgets.h"

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
		ClassDumper3::Log("ClassInspector : Main window does not exist!!");
		exit(-1);
	}
}

void ClassInspector::Draw()
{
	if (!SelectedClass)
		return;

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

	ImGui::Columns(2, nullptr, false);
	ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() / 2);

	if (ImGui::Button("Copy To Clipboard"))
	{
		CopyInfo();
	}

	if (ImGui::Button("Scan for Code References"))
	{
		RTTIObserver->ScanForCodeReferencesAsync(SelectedClass);
	}
	ImGui::SameLine();

	if (ImGui::Button("Scan for Instances"))
	{
		RTTIObserver->ScanForClassInstancesAsync(SelectedClass);
	}

	if (RTTIObserver->IsAsyncScanning())
	{
		ImGui::Text("Performing Scan Operation...");
		ImGui::SameLine();
		ImGui::Spinner("Spinner", 10, 10, 0xFF0000FF);
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
		for (std::weak_ptr<_Class> InterfaceWeak : SelectedClass->Interfaces)
		{
			std::shared_ptr<_Class> Interface = InterfaceWeak.lock();
			if (!Interface)
				continue;
			ImGui::Text(Interface->Name.c_str());
		}
	}

	ImGui::Text("Virtual Function Table: 0x%s", IntegerToHexStr(SelectedClass->VTable).c_str());

	ImGui::Text("Num Virtual Functions: %d", SelectedClass->Functions.size());
	{
		ScopedColor Color(ImGuiCol_Text, Color::Green);
		int Index = 0;
		for (auto& Function : SelectedClass->FunctionNames)
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
	ImGui::NextColumn();
	DrawClassReferences();

	ImGui::End();
}


void ClassInspector::DrawClassReferences()
{
	if (!SelectedClass) return;

	
	ImGui::Text("Code References:");
	ImGui::BeginChildFrame(2, {300,300});
	if (!RTTIObserver->IsAsyncScanning())
	{
		for (auto& CodeReference : SelectedClass->CodeReferences)
		{
			std::string CodeRefString = "0x" + IntegerToHexStr(CodeReference);

			ImGui::Text(CodeRefString.c_str());
			if (ImGui::IsItemClicked(EMouseButton::Left))
			{
				// disassemble window
			}
			else if (ImGui::IsItemClicked(EMouseButton::Right))
			{
				ClassDumper3::CopyToClipboard(CodeRefString);
			}
		}
	}
	ImGui::EndChildFrame();

	ImGui::Text("Instances:");
	ImGui::BeginChildFrame(3, { 300,300 }, ImGuiWindowFlags_NoCollapse);
	if (!RTTIObserver->IsAsyncScanning())
	{
		for (auto& Instance : SelectedClass->ClassInstances)
		{
			std::string InstanceStr = "0x" + IntegerToHexStr(Instance);

			ImGui::Text(InstanceStr.c_str());

			if (ImGui::IsItemClicked(EMouseButton::Right))
			{
				ClassDumper3::CopyToClipboard(InstanceStr);
			}
		}
	}
	ImGui::EndChildFrame();
}

void ClassInspector::OnProcessSelectedDelegate(std::shared_ptr<FTargetProcess> InTarget, std::shared_ptr<RTTI> InRTTI)
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

	ClassDumper3::CopyToClipboard(Info);
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