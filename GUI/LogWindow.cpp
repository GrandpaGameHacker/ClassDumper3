#include "LogWindow.h"
#include <chrono>
#include <iomanip>
#include <sstream> 

LogWindow::LogWindow()
{
	LogHistory.reserve(MaxLogHistory);
	std::string logFileName = "log_" + GetCurrentDateTime() + ".txt";
	LogFile.open(logFileName, std::ios::out | std::ios::app);
}

LogWindow::~LogWindow()
{
	LogFile.close();
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
	
	if (ImGui::Button("Clear Log"))
	{
		Clear();
	}
	
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
	LogFile << InLog << "\n";
	if (FlushCounter > FlushCounterMax)
	{
		std::flush(LogFile);
		FlushCounter = 0;
	}
	FlushCounter++;
	WrapHistoryMax();
}

void LogWindow::Log(const char* InLog)
{
	std::scoped_lock Lock(LogMutex);
	LogHistory.push_back(InLog);
	
	LogFile << InLog << "\n";
	if (FlushCounter > FlushCounterMax)
	{
		std::flush(LogFile);
		FlushCounter = 0;
	}
	FlushCounter++;
	
	WrapHistoryMax();
}

void LogWindow::Clear()
{
	std::scoped_lock Lock(LogMutex);
	LogHistory.clear();
}

void LogWindow::WrapHistoryMax()
{
	if (LogHistory.size() > MaxLogHistory)
	{
		LogHistory.erase(LogHistory.begin(), LogHistory.end() - MaxLogHistory);
	}
}

std::string LogWindow::GetCurrentDateTime()
{
	auto now = std::chrono::system_clock::now();
	auto in_time_t = std::chrono::system_clock::to_time_t(now);

	std::stringstream ss;
	ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d_%H%M%S");
	return ss.str();
}
