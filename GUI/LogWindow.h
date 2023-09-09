#pragma once
#include "Interfaces/IWindow.h"
#include <vector>
#include <string>
#include <mutex>
#include <fstream> 

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
	void WrapHistoryMax();
	std::string GetCurrentDateTime();
	std::vector<std::string> LogHistory;
	size_t MaxLogHistory = 256;
	std::mutex LogMutex;
	std::ofstream LogFile;
	size_t FlushCounter = 0;
	size_t FlushCounterMax = 1024;
};

