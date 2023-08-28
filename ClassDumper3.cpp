#include "ClassDumper3.h"

std::shared_ptr<LogWindow> ClassDumper3::LogWnd = nullptr;

int ClassDumper3::Run()
{
	WindowClass = { sizeof(WNDCLASSEX), CS_CLASSDC, DXApp.WndProc, 0L, 0L, NULL, NULL, NULL, NULL, NULL, "ClassDumper2", NULL };
	RegisterClassEx(&WindowClass);
	//primary monitor only. Multiple screen support come later
	int w = GetSystemMetrics(SM_CXSCREEN);
	int h = GetSystemMetrics(SM_CYSCREEN);

	IWindow::Width = w;
	IWindow::Height = h;

	DXWindow = CreateWindow(
		WindowClass.lpszClassName,
		"ClassDumper 3", WS_OVERLAPPEDWINDOW,
		0, 0, w, h, NULL, NULL,
		WindowClass.hInstance, NULL
	);

	if (!DXApp.CreateDeviceD3D(DXWindow)) {
		DXApp.CleanupDeviceD3D();
		DestroyWindow(DXWindow);
		UnregisterClass(WindowClass.lpszClassName, WindowClass.hInstance);
		exit(1);
		return 1;
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
	
	Initialize();
	GUILoop();
	
	CleanExit();
	return 0;
}

void ClassDumper3::Initialize()
{
	MainWnd = IWindow::Create<MainWindow>();
	MainWnd->Enable();
	
	InspectorWnd = IWindow::Create<ClassInspector>();
	InspectorWnd->InitializeBindings();

	LogWnd = IWindow::Create<LogWindow>();
	LogWnd->Enable();
}

void ClassDumper3::CleanExit()
{
	//DXApp.WaitForLastSubmittedFrame();
	DXApp.ShutdownBackend();
	DXApp.CleanupDeviceD3D();

	DestroyWindow(DXWindow);
	UnregisterClass(WindowClass.lpszClassName, WindowClass.hInstance);
	exit(0);
}

void ClassDumper3::GUILoop()
{
	bool done = false;
	while (!done)
	{
		MSG msg;
		while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT)
				done = true;
		}
		if (done)
			break;
		DXApp.CreateFrame();

		IWindow::bDrawingFrame = true;
		
		for (auto& Window : IWindow::WindowList)
		{
			Window->Tick();
		}
		
		IWindow::bDrawingFrame = false;
		
		if (IWindow::DeferredWindowList.size())
		{
			IWindow::WindowList.insert(IWindow::WindowList.end(), IWindow::DeferredWindowList.begin(), IWindow::DeferredWindowList.end());
			IWindow::DeferredWindowList.clear();
		}
		
		DXApp.RenderFrame();
	}
}

void ClassDumper3::Log(const std::string& InLog)
{
	if (!LogWnd) return;
	LogWnd->Log(InLog);
}

void ClassDumper3::Log(const char* InLog)
{
	if (!LogWnd) return;
	LogWnd->Log(InLog);
}

void ClassDumper3::LogF(std::string Format, ...)
{
	if (!LogWnd) return;
	const char* FormatC = Format.c_str();
	if (!LogWnd) return;

	va_list Args;
	va_start(Args, Format);

	char Buffer[4096];
	vsnprintf_s(Buffer, sizeof(Buffer), FormatC, Args);

	va_end(Args);

	LogWnd->Log(Buffer);
}

void ClassDumper3::LogF(const char* Format, ...)
{
	if (!LogWnd) return;

	va_list Args;
	va_start(Args, Format);

	char Buffer[4096];
	vsnprintf_s(Buffer, sizeof(Buffer), Format, Args);

	va_end(Args);

	LogWnd->Log(Buffer);
}

void ClassDumper3::ClearLog()
{
	if (!LogWnd) return;
	LogWnd->Clear();
}

int main()
{
	ClassDumper3 Dumper;
	Dumper.Run();
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hInstPrev, PSTR cmdline, int cmdshow)
{
	ClassDumper3 Dumper;
	Dumper.Run();
}