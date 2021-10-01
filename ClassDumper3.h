#pragma once
#include <windows.h>
#include "flecs/flecs.h"
#include "DirectX/ImGuiApp.h"
class ClassDumper3
{
	ClassDumper3() {};
	ClassDumper3(ClassDumper3 const&) = delete;
	void operator=(ClassDumper3 const&) = delete;

public:
	static void AppMain();
	static HWND DXWindow;
	static WNDCLASSEX WindowClass;
	static ImGuiApp DXApp;
};

