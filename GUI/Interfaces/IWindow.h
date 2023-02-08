#pragma once
#include "imgui.h"
#include <vector>

enum class EWindowType
{
	MainWindow,
	ClassInspector,
	None,
};

class IWindow
{
public:
	IWindow();
	virtual ~IWindow();
	bool operator==(const IWindow& RHS);
	virtual void Tick();
	virtual void Draw() = 0;
	virtual bool IsShown();
	virtual bool Enable();
	virtual bool Disable();
	virtual EWindowType GetWindowType();
	
	template <typename WindowType>
	static WindowType* GetWindow(EWindowType Type)
	{
		for (IWindow* Window : WindowList)
		{
			if (Window)
			{
				if (Window->GetWindowType() == Type)
				{
					return static_cast<WindowType*>(Window);
				}
			}
		}
		return nullptr;
	}
	
	static std::vector<IWindow*> WindowList;
	static int Width, Height;
private:
	size_t WindowID;
	bool bEnabled = false;
};

