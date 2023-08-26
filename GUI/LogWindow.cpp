#include "LogWindow.h"

LogWindow::LogWindow()
{
}

LogWindow::~LogWindow()
{
}

void LogWindow::Draw()
{
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 screenSize = io.DisplaySize;

	float windowHeight = screenSize.y / 4;  // 1/4 of the screen height
	ImVec2 windowSize(screenSize.x, windowHeight);

	float windowPosY = screenSize.y - windowHeight;  // Position the window at the bottom

	ImGui::SetNextWindowPos(ImVec2(0, windowPosY));
	ImGui::SetNextWindowSize(windowSize);
	
	ImGui::Begin("Log", nullptr, ImGuiWindowFlags_NoCollapse);
	size_t Idx = 0;
	ImGui::BeginChild("Scrolling");
	{
		std::scoped_lock Lock(LogMutex);
		for (auto It = LogHistory.rbegin(); It != LogHistory.rend(); It++)
		{
			ImGui::Text("%s", It->c_str());
			Idx++;
			if (Idx >= MaxLogHistory)
			{
				break;
			}
		}
	}
	
	ImGui::EndChild();
	ImGui::End();
}

void LogWindow::Log(const std::string& InLog)
{
	std::scoped_lock Lock(LogMutex);
	LogHistory.push_back(InLog);
}

void LogWindow::Log(const char* InLog)
{
	std::scoped_lock Lock(LogMutex);
	LogHistory.push_back(InLog);
}

void LogWindow::Clear()
{
	std::scoped_lock Lock(LogMutex);
	LogHistory.clear();
}
