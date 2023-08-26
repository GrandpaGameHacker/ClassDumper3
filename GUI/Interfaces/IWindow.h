#pragma once
#include "imgui.h"
#include <vector>
#include <memory>

enum EMouseButton
{
	Left,
	Right
};

namespace Color
{
	constexpr ImVec4 White = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	constexpr ImVec4 Black = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	constexpr ImVec4 Red = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
	constexpr ImVec4 Green = ImVec4(0.0f, 1.0f, 0.0f, 1.0f);
	constexpr ImVec4 Blue = ImVec4(0.0f, 0.0f, 1.0f, 1.0f);
	constexpr ImVec4 Yellow = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
	constexpr ImVec4 Cyan = ImVec4(0.0f, 1.0f, 1.0f, 1.0f);
	constexpr ImVec4 Magenta = ImVec4(1.0f, 0.0f, 1.0f, 1.0f);
}

class ScopedColor
{
public:
	ScopedColor(ImGuiCol type, ImVec4 color)
	{
		ImGui::PushStyleColor(type, color);
	}

	~ScopedColor()
	{
		ImGui::PopStyleColor();
	}
};

class IWindow
{
public:
	IWindow();
	virtual ~IWindow();
	bool operator==(const IWindow& RHS);
	virtual void Tick();
	virtual void Draw() {};
	virtual bool IsShown();
	virtual bool Enable();
	virtual bool Disable();;
	
	template <typename WindowType>
	static std::shared_ptr<WindowType> Create()
	{
		std::shared_ptr<WindowType> Window = std::make_shared<WindowType>();
		WindowList.push_back(Window);
		return Window;
	}
		
	template <typename WindowType>
	static std::shared_ptr<WindowType> GetWindow()
	{
		for (std::shared_ptr<IWindow> Window : WindowList)
		{
			if (!Window) continue;
			
			auto castedWindow = std::dynamic_pointer_cast<WindowType>(Window);
			if(!castedWindow) continue;	
			
			return castedWindow;
		}
		return nullptr;
	}
	
	static std::vector<std::shared_ptr<IWindow>> WindowList;
	static int Width, Height;
private:
	size_t WindowID;
	bool bEnabled = false;
};

