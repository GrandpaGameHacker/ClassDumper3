#pragma once
#include "imgui.h"
#include <vector>
#include <memory>

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
	static std::shared_ptr<WindowType> Create()
	{
		std::shared_ptr<WindowType> Window = std::make_shared<WindowType>();
		WindowList.push_back(Window);
		return Window;
	}
		
	template <typename WindowType>
	static std::shared_ptr<WindowType> GetWindow(EWindowType Type)
	{
		for (std::shared_ptr<IWindow> Window : WindowList)
		{
			if (Window)
			{
				if (Window->GetWindowType() == Type)
				{
					return static_pointer_cast<WindowType>(Window);
				}
			}
		}
		return nullptr;
	}
	
	static std::vector<std::shared_ptr<IWindow>> WindowList;
	static int Width, Height;
private:
	size_t WindowID;
	bool bEnabled = false;
};

