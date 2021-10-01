#include "ClassDumper3.h"
#include "GUI/SharedState.h"
HWND ClassDumper3::DXWindow;
WNDCLASSEX ClassDumper3::WindowClass;
ImGuiApp ClassDumper3::DXApp;

int main(int argc, char* argv[]) {
	ClassDumper3::AppMain();
};

void ClassDumper3::AppMain()
{
	WindowClass = { sizeof(WNDCLASSEX), CS_CLASSDC, DXApp.WndProc, 0L, 0L, NULL, NULL, NULL, NULL, NULL, "ClassDumper2", NULL };
	RegisterClassEx(&WindowClass);

	DXWindow = CreateWindow(
		WindowClass.lpszClassName,
		"ClassDumper 2", WS_OVERLAPPEDWINDOW,
		100, 100, 1280, 800, NULL, NULL,
		WindowClass.hInstance, NULL
	);

	if (!DXApp.CreateDeviceD3D(DXWindow)) {
		DXApp.CleanupDeviceD3D();
		DestroyWindow(DXWindow);
		UnregisterClass(WindowClass.lpszClassName, WindowClass.hInstance);
		exit(0);
	}
	SetWindowLong(DXWindow, GWL_STYLE, 0);
	ShowWindow(DXWindow, SW_SHOWMAXIMIZED);
	UpdateWindow(DXWindow);
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.IniFilename = NULL;
	ImGui::StyleColorsDark();
	DXApp.SetupBackend();

	// do app stuff
	SS::InitState();
	SS::ApplicationLoop();
	// exit
	exit(0);
}
