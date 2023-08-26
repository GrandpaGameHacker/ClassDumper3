#pragma once
#include "Interfaces/IWindow.h"
#include <vector>
#include <string>
#include <mutex>

class LogWindow : public IWindow
{
public:
	LogWindow();
	~LogWindow();
	void Draw() override;
	void Log(const std::string& InLog);
	void Log(const char* InLog);
	void LogF(const std::string& Format, ...);
	void LogF(const char* Format, ...);
	void Clear();
protected:
	std::vector<std::string> LogHistory;
	size_t MaxLogHistory = 100;
	std::mutex LogMutex;
};

