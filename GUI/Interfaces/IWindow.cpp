#include "IWindow.h"
std::vector<std::shared_ptr<IWindow>> IWindow::WindowList;
std::vector<std::shared_ptr<IWindow>> IWindow::DeferredWindowList;
int IWindow::Height;
int IWindow::Width;

bool IWindow::bDrawingFrame = false;

IWindow::IWindow()
{
    static size_t WindowIDCount = 0;
    WindowID = WindowIDCount++;
}

IWindow::~IWindow()
{
    for (size_t i = 0; i < IWindow::WindowList.size(); i++)
    {
        if (IWindow::WindowList[i].get() == this)
        {
            IWindow::WindowList.erase(IWindow::WindowList.begin() + i);
        }
    }
    
	for (size_t i = 0; i < IWindow::DeferredWindowList.size(); i++)
	{
		if (IWindow::DeferredWindowList[i].get() == this)
		{
			IWindow::DeferredWindowList.erase(IWindow::DeferredWindowList.begin() + i);
		}
	}
}

bool IWindow::operator==(const IWindow& RHS)
{
        return WindowID == RHS.WindowID;
}

void IWindow::Tick()
{
    if (bEnabled)
    {
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