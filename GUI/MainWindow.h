#pragma once
#include "Interfaces/IWindow.h"
#include "../Delegate.h"
#include "../W32/Memory.h"
#include "../W32/RTTI.h"
#include <memory>

class MainWindow : public IWindow
{
public:
	MainWindow();
	~MainWindow();
	void Draw() override;
	
	MulticastDelegate<std::shared_ptr<ClassMetaData>> OnClassSelected;
	MulticastDelegate<std::shared_ptr<FTargetProcess>, std::shared_ptr<RTTI>> OnProcessSelected;
protected:
	void DrawProcessList();
	void DrawModuleList();
	void RefreshProcessList();
	void SelectProcess();
	void FilterClasses(const std::string& filter);
	void DrawClassList();
	void DrawClass(const std::shared_ptr<ClassMetaData>& cl);
	
	std::string SelectedProcessName;
	std::string SelectedModuleName;
	std::string ProcessFilter;
	std::string ClassFilter;
	std::vector<std::shared_ptr<ClassMetaData>> FilteredClassesCache;
	std::vector<FProcessListItem> ProcessList;
	std::shared_ptr<FTargetProcess> Target;
	std::shared_ptr<RTTI> RTTIObserver;
	std::weak_ptr<ClassMetaData> SelectedClass;
};