#include "IWindow.h"
std::vector<std::shared_ptr<IWindow>> IWindow::WindowList;

int IWindow::Height;
int IWindow::Width;

IWindow::IWindow()
{
    static size_t WindowIDCount = 0;
    WindowID = WindowIDCount++;
}

IWindow::~IWindow() {
    for (size_t i = 0; i < IWindow::WindowList.size(); i++)
    {
        if (IWindow::WindowList[i].get() == this)
        {
            IWindow::WindowList.erase(IWindow::WindowList.begin() + i);
        };
    }
}

bool IWindow::operator==(const IWindow& RHS)
{
        return WindowID == RHS.WindowID;
}

void IWindow::Tick()
{
    if (bEnabled) {
        Draw();
    }
}

bool IWindow::IsShown()
{
    return bEnabled;
}

bool IWindow::Enable()
{
    bEnabled = true;
    return true;
}

bool IWindow::Disable()
{
    bEnabled = false;
    return false;
}

EWindowType IWindow::GetWindowType()
{
    return EWindowType::None;
}
