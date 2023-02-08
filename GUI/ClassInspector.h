#pragma once
#include "Interfaces/IWindow.h"
#include "../Delegate.h"
#include "../W32/Memory.h"
#include "../W32/RTTI.h"
#include <memory>

class ClassInspector : public IWindow
{
public:
	ClassInspector();
	~ClassInspector();
	void Draw() override;
	EWindowType GetWindowType() override { return EWindowType::ClassInspector; };
protected:
	void DrawClass();
	void OnProcessSelectedDelegate(std::shared_ptr<TargetProcess> target, std::shared_ptr<RTTI> rtti);
	void OnClassSelectedDelegate(std::shared_ptr<_Class> cl);
	
	std::shared_ptr<_Class> SelectedClass;
	std::shared_ptr<TargetProcess> Target;
	std::shared_ptr<RTTI> RTTIObserver;
};