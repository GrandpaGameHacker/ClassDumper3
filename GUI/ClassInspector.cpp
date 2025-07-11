
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
	if (!SelectedClassWeak)
	{
		return;
	}

	ImVec2 screenSize = ImGui::GetIO().DisplaySize;
	ImVec2 WindowSize(screenSize.x * 0.4, screenSize.y * 0.5f);

	ImGui::SetNextWindowSize(WindowSize, ImGuiCond_FirstUseEver);

	const char* WindowTitle = "Class Inspector###";

	if (SelectedClassWeak->bInterface)
	{
		WindowTitle = "Interface Inspector###";
	}
	else if (SelectedClassWeak->bStruct)
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
		RTTIObserver->ScanForCodeReferencesAsync(SelectedClassWeak);
	}
	ImGui::SameLine();

	if (ImGui::Button("Scan for Instances"))
	{
		RTTIObserver->ScanForClassInstancesAsync(SelectedClassWeak);
	}

	if (RTTIObserver->IsAsyncScanning())
	{
		ImGui::Text("Performing Scan Operation...");
		ImGui::SameLine();
		ImGui::Spinner("Spinner", 10, 10, 0xFF0000FF);
	}

	ImGui::Separator();
	ImGui::Text("Name: %s", SelectedClassWeak->Name.c_str());

	ImGui::Text("CompleteObjectLocator: 0x%s", IntegerToHexStr(SelectedClassWeak->CompleteObjectLocator).c_str());

	ImGui::Text("Num Inherited: %d", SelectedClassWeak->Parents.size());
	{
		for (const std::shared_ptr<ParentClass>& Parent : SelectedClassWeak->Parents)
		{
			std::shared_ptr<ClassMetaData> ParentData = Parent->Class.lock();
			// Optional color based on type
			if (ParentData && ParentData->bInterface)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 1, 1));
			else if (ParentData && ParentData->bStruct)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0, 0, 1.0f));
			else
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1.0f));

			// Apply indentation based on tree depth
			ImGui::Indent(Parent->TreeDepth * 12.0f); // Adjust multiplier as needed

			ImGui::TextUnformatted(Parent->Name.c_str());

			ImGui::Unindent(Parent->TreeDepth * 12.0f);
			ImGui::PopStyleColor();
		}
	}


	ImGui::Text("Num Interfaces: %d", SelectedClassWeak->Interfaces.size());
	{
		ScopedColor Color(ImGuiCol_Text, Color::Magenta);
		for (const std::weak_ptr<ClassMetaData>& InterfaceWeak : SelectedClassWeak->Interfaces)
		{
			std::shared_ptr<ClassMetaData> Interface = InterfaceWeak.lock();
			if (!Interface)
				continue;
			ImGui::Text(Interface->Name.c_str());
		}
	}

	ImGui::Text("Virtual Function Table: 0x%s", IntegerToHexStr(SelectedClassWeak->VTable).c_str());

	ImGui::Text("Num Virtual Functions: %d", static_cast<int>(SelectedClassWeak->Functions.size()));
	{
		ScopedColor Color(ImGuiCol_Text, Color::Green);
		int Index = 0;

		for (const auto& Function : SelectedClassWeak->Functions)
		{
			auto it = SelectedClassWeak->FunctionNames.find(Function);
			std::string FunctionName = (it != SelectedClassWeak->FunctionNames.end()) ? it->second : "<unknown>";
			// todo refactor function names to be global per process.

			std::string FunctionText = std::to_string(Index) + " - " + IntegerToHexStr(Function) + " : " + FunctionName;
			ImGui::Text("%s", FunctionText.c_str());

			if (ImGui::IsItemClicked(EMouseButton::Left))
			{
				// insert disassembler tool here
			}
			if (ImGui::IsItemClicked(EMouseButton::Right) && it != SelectedClassWeak->FunctionNames.end())
			{
				RenameFunction(&(*it)); 
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
	if (!SelectedClassWeak) return;
	
	ImGui::Text("Code References:");
	ImGui::BeginChildFrame(2, {300,300});
	if (!RTTIObserver->IsAsyncScanning())
	{
		for (const auto& CodeReference : SelectedClassWeak->CodeReferences)
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
		for (const auto& Instance : SelectedClassWeak->ClassInstances)
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

void ClassInspector::OnClassSelectedDelegate(std::shared_ptr<ClassMetaData> InClass)
{
	SelectedClassWeak = InClass;
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
    std::string Info = "Name: " + SelectedClassWeak->Name + "\n";
	Info += "CompleteObjectLocator: 0x" + IntegerToHexStr(SelectedClassWeak->CompleteObjectLocator) + "\n";
	Info += "Num Inherited: " + std::to_string(SelectedClassWeak->Parents.size()) + "\n";

    for (const std::shared_ptr<ParentClass>& Parent : SelectedClassWeak->Parents)
    {
		Info += Parent->Name + "\n";
    }

	Info += "Num Interfaces: " + std::to_string(SelectedClassWeak->Interfaces.size()) + "\n";
	Info += "Virtual Function Table: 0x" + IntegerToHexStr(SelectedClassWeak->VTable) + "\n";
	Info += "Num Virtual Functions: " + std::to_string(SelectedClassWeak->Functions.size()) + "\n";

    for (const auto& FunctionName : SelectedClassWeak->FunctionNames)
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