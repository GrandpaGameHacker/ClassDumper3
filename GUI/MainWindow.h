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
	
	MulticastDelegate<std::shared_ptr<_Class>> OnClassSelected;
	MulticastDelegate<std::shared_ptr<FTargetProcess>, std::shared_ptr<RTTI>> OnProcessSelected;
protected:
	void DrawProcessList();
	void RefreshProcessList();
	void SelectProcess();
	void FilterClasses(const std::string& filter);
	void DrawClassList();
	void DrawClass(const std::shared_ptr<_Class>& cl);
	
	std::string selectedProcessName;
	std::string processFilter;
	std::string classFilter;
	std::vector<std::shared_ptr<_Class>> filteredClassesCache;
	std::vector<FProcessListItem> ProcessList;
	std::shared_ptr<FTargetProcess> Target;
	std::shared_ptr<RTTI> RTTIObserver;
	std::weak_ptr<_Class> SelectedClass;
};