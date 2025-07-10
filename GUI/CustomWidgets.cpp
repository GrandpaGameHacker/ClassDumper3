#include "CustomWidgets.h"
#include "../W32/Disassembler.h"
bool ImGui::BufferingBar(const char* label, float value, const ImVec2& size_arg, const ImU32& bg_col, const ImU32& fg_col)
{
		ImGuiWindow* window = GetCurrentWindow();
		if (window->SkipItems)
		{
			return false;
		}

		const ImGuiContext& Context = *GImGui;
		const ImGuiStyle& style = Context.Style;
		const ImGuiID id = window->GetID(label);

		ImVec2 pos = window->DC.CursorPos;
		ImVec2 size = size_arg;
		size.x -= style.FramePadding.x * 2;

		const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
		ItemSize(bb, style.FramePadding.y);
		if (!ItemAdd(bb, id))
		{
			return false;
		}

		// Render
		const float circleStart = size.x * 0.7f;
		const float circleEnd = size.x;
		const float circleWidth = circleEnd - circleStart;

		window->DrawList->AddRectFilled(bb.Min, ImVec2(pos.x + circleStart, bb.Max.y), bg_col);
		window->DrawList->AddRectFilled(bb.Min, ImVec2(pos.x + circleStart * value, bb.Max.y), fg_col);

		const auto t = static_cast<float>(Context.Time);
		const float r = size.y / 2;
		const float speed = 1.5f;

		const float a = speed * 0;
		const float b = speed * 0.333f;
		const float c = speed * 0.666f;

		const float o1 = (circleWidth + r) * (t + a - speed * (int)((t + a) / speed)) / speed;
		const float o2 = (circleWidth + r) * (t + b - speed * (int)((t + b) / speed)) / speed;
		const float o3 = (circleWidth + r) * (t + c - speed * (int)((t + c) / speed)) / speed;

		window->DrawList->AddCircleFilled(ImVec2(pos.x + circleEnd - o1, bb.Min.y + r), r, bg_col);
		window->DrawList->AddCircleFilled(ImVec2(pos.x + circleEnd - o2, bb.Min.y + r), r, bg_col);
		window->DrawList->AddCircleFilled(ImVec2(pos.x + circleEnd - o3, bb.Min.y + r), r, bg_col);
		return true;
}

bool ImGui::Spinner(const char* label, float radius, int thickness, const ImU32& color)
{
	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems)
	{
		return false;
	}

	const ImGuiContext& Context = *GImGui;
	const ImGuiStyle& style = Context.Style;
	const ImGuiID id = window->GetID(label);

	ImVec2 pos = window->DC.CursorPos;
	ImVec2 size((radius) * 2, (radius + style.FramePadding.y) * 2);

	const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
	ItemSize(bb, style.FramePadding.y);
	if (!ItemAdd(bb, id))
	{
		return false;
	}

	// Render
	window->DrawList->PathClear();

	int num_segments = 30;
	const auto time = static_cast<float>(Context.Time);
	int start = abs((int)(ImSin(time * 1.8f) * (num_segments - 5)));

	const float a_min = IM_PI * 2.0f * ((float)start) / (float)num_segments;
	const float a_max = IM_PI * 2.0f * ((float)num_segments - 3) / (float)num_segments;

	const ImVec2 centre = ImVec2(pos.x + radius, pos.y + radius + style.FramePadding.y);

	for (int i = 0; i < num_segments; i++)
	{
		const float a = a_min + ((float)i / (float)num_segments) * (a_max - a_min);
		window->DrawList->PathLineTo(ImVec2(centre.x + ImCos(a + time * 8) * radius,
			centre.y + ImSin(a + time * 8) * radius));
	}

	window->DrawList->PathStroke(color, false, static_cast<float>(thickness));

	return true;
}

#include "imgui.h"

IMGUI_API bool ImGui::HexEditor(const char* label, void* data, size_t dataSize, size_t* dataSizeRemaining, size_t dataPreviewOffset /*= 0*/, size_t dataPreviewSize /*= 0*/)
{
	if (!data || dataSize == 0)
		return false;

	bool valueChanged = false;
	unsigned char* byteData = reinterpret_cast<unsigned char*>(data);

	int columns = 16;
	char asciiData[17] = { 0 }; // For ASCII representation

	ImGui::BeginChild(label, ImVec2(0, ImGui::GetTextLineHeight() * 20 + 20), true);

	ImGuiListClipper clipper;
	clipper.Begin(dataSize / columns);
	while (clipper.Step())
	{
		for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; line++)
		{
			size_t offset = line * columns;
			ImGui::Text("%04zX: ", offset);
			ImGui::SameLine();

			// Hexadecimal data
			for (int col = 0; col < columns && offset + col < dataSize; col++)
			{
				char label_format[32];
				sprintf_s(label_format, "%02X##%zu", byteData[offset + col], offset + col);

				if (ImGui::InputTextWithHint(label_format, "", label_format + 3, 3, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue))
				{
					unsigned int value;
					if (sscanf_s(label_format + 3, "%02X", &value) == 1)
					{
						byteData[offset + col] = (unsigned char)value;
						valueChanged = true;
					}
				}
				ImGui::SameLine();

				// ASCII representation
				asciiData[col] = (byteData[offset + col] >= 32 && byteData[offset + col] < 127) ? byteData[offset + col] : '.';
			}

			ImGui::Text(" %s", asciiData);
			memset(asciiData, 0, sizeof(asciiData)); // Reset for next line
		}
	}

	if (dataSizeRemaining)
	{
		*dataSizeRemaining = dataSize - (dataPreviewOffset + dataPreviewSize);
	}

	ImGui::EndChild();

	return valueChanged;
}

IMGUI_API bool ImGui::Disassembly(const char* label, void* data, size_t dataSize, size_t* dataSizeRemaining, size_t dataPreviewOffset /*= 0*/, size_t dataPreviewSize /*= 0*/)
{
	return false;
}

