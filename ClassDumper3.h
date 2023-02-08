#pragma once
#include <Windows.h>
#include "RenderConfig.h"
#include "GUI/Helpers/ImGuiApp.h"
#include "GUI/Interfaces/IWindow.h"
#include "GUI/MainWindow.h"
#include "GUI/ClassInspector.h"

#include <memory>

class ClassDumper3
{
public:
	ClassDumper3() {};
	int Run();
	void Initialize();
	void CleanExit();
	void GUILoop();
	
	HWND DXWindow;
	WNDCLASSEX WindowClass;
#ifdef USE_DX12
	ImGuiAppDX12 DXApp;
#else
	ImGuiAppDX11 DXApp;
#endif
	
	std::shared_ptr<MainWindow> MainWnd;
	std::shared_ptr<ClassInspector> InspectorWnd;
};