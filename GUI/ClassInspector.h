#pragma once
#include "Interfaces/IWindow.h"
#include "../Delegate.h"
#include "../W32/Memory.h"
#include "../W32/RTTI.h"
#include <memory>

class RenamePopup : public IWindow
{
public:
	RenamePopup() {};
	~RenamePopup(){};
	void Initialize(std::pair<const uintptr_t, std::string>* func);
	void Draw() override;
protected:
	std::string new_name;
	std::pair<const uintptr_t, std::string>* func_ref = nullptr;
};

class ClassInspector : public IWindow, public std::enable_shared_from_this<ClassInspector>
{
public:
	ClassInspector();
	~ClassInspector();
	void InitializeBindings();
	void Draw() override;
	EWindowType GetWindowType() override { return EWindowType::ClassInspector; };
protected:
	void DrawClass();
	void OnProcessSelectedDelegate(std::shared_ptr<TargetProcess> target, std::shared_ptr<RTTI> rtti);
	void OnClassSelectedDelegate(std::shared_ptr<_Class> cl);
	void RenameFunction(std::pair<const uintptr_t, std::string>* func);
	void CopyInfo();
	
	std::shared_ptr<_Class> SelectedClass;
	std::shared_ptr<TargetProcess> Target;
	std::shared_ptr<RTTI> RTTIObserver;

	// popups
	std::shared_ptr<RenamePopup> RenamePopupWnd;
};