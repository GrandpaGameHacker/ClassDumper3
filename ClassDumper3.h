#pragma once
#include <Windows.h>
#include "RenderConfig.h"
#include "GUI/Helpers/ImGuiApp.h"
#include "GUI/Interfaces/IWindow.h"
#include "GUI/MainWindow.h"
#include "GUI/ClassInspector.h"
#include "GUI/LogWindow.h"

#include <memory>

class ClassDumper3
{
public:
	ClassDumper3() {};
	int Run();
	void Initialize();
	void CleanExit();
	void GUILoop();

	static void Log(const std::string& InLog);
	static void Log(const char* InLog);
	static void LogF(std::string Format, ...);
	static void LogF(const char* Format, ...);
	static void ClearLog();

	static void CopyToClipboard(const std::string& InString);
	
	
	HWND DXWindow;
	WNDCLASSEX WindowClass;
#ifdef USE_DX12
	ImGuiAppDX12 DXApp;
#else
	ImGuiAppDX11 DXApp;
#endif
	
private:
	std::shared_ptr<MainWindow> MainWnd;
	std::shared_ptr<ClassInspector> InspectorWnd;
	
	friend LogWindow;
	static std::shared_ptr<LogWindow> LogWnd;
};