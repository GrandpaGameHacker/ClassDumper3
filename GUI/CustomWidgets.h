#pragma once

#include <Windows.h>
#include "imgui.h"
#include "imgui_internal.h"

namespace ImGui
{
	IMGUI_API bool BufferingBar(const char* label, float value, const ImVec2& size_arg, const ImU32& bg_col, const ImU32& fg_col);
	IMGUI_API bool Spinner(const char* label, float radius, int thickness, const ImU32& color);
	IMGUI_API bool HexEditor(const char* label, void* data, size_t dataSize, size_t* dataSizeRemaining, size_t dataPreviewOffset = 0, size_t dataPreviewSize = 0);
	IMGUI_API bool Disassembly(const char* label, void* data, size_t dataSize, size_t* dataSizeRemaining, size_t dataPreviewOffset = 0, size_t dataPreviewSize = 0);
}