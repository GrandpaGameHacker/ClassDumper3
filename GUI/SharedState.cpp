#include "SharedState.h"
#include "WindowComponents.h"
#include "..\ClassDumper3.h"
#include <iostream>
#include <Windows.h>

flecs::world SS::ecs;
flecs::entity SS::App;

bool SS::bRunning = true;
bool SS::bImportPopup = false;
bool SS::bExportPopup = false;

char* SS::szProcess = nullptr;
PROCESSENTRY32* SS::targetProcess;
HANDLE SS::hProcess = INVALID_HANDLE_VALUE;
std::vector<PROCESSENTRY32*> SS::processList;

char* SS::szModule;
MODULEENTRY32* SS::targetModule;
std::vector<MODULEENTRY32*> SS::moduleList;

static bool bInit = false;

std::vector<uintptr_t> SS::VTables;
bool SS::bSectionInfoGood = false;
bool SS::bFoundVTables = false;

SectionInfo* SS::targetSectionInfo;

struct DXRender {};

void SharedState::InitState()
{
    if (!bInit) {
        bInit = true;
        ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
        App = ecs.entity("App")
            .add<DXRender>()
            .add<MainWindowComponent>()
            .add<ClassViewerComponent>();

        ecs.system<DXRender>()
            .kind(flecs::PreFrame)
            .each([](DXRender& r) {ClassDumper3::DXApp.CreateFrame(); });

        ecs.system<DXRender>()
            .kind(flecs::OnStore)
            .each([](DXRender& r) {ClassDumper3::DXApp.RenderFrame(); });

        ecs.system<MainWindowComponent>()
            .kind(flecs::PreStore)
            .each(MainWindowSystem);

        ecs.system<ClassViewerComponent>()
            .kind(flecs::PreStore)
            .each(ClassViewerSystem);

        processList = GetProcessList();
        szProcess = processList[0]->szExeFile;
        targetProcess = processList[0];
    }
}

void SharedState::ApplicationLoop()
{
    while (bRunning)
    {
        MSG msg;
        while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                bRunning = false;
        }
        if (!bRunning)
            break;
        ecs.progress();
    }

}

void SharedState::CopyToClipboard(const char* format, ...)
{
    char buffer[1024] = { 0 };
    va_list v1;
    va_start(v1, format);
    vsnprintf(buffer, 1024, format, v1);
    va_end(v1);
    ImGui::SetClipboardText(buffer);
}

