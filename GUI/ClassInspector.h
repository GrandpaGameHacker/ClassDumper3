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
	void Initialize(std::pair<const uintptr_t, std::string>* InFunction);
	void Draw() override;
protected:
	std::string NewName;
	std::pair<const uintptr_t, std::string>* FunctionPtr = nullptr;
};

class ClassInspector : public IWindow, public std::enable_shared_from_this<ClassInspector>
{
public:
	ClassInspector();
	~ClassInspector();
	void InitializeBindings();
	void Draw() override;
protected:
	void DrawClass();
	void DrawCodeReferences();
	void OnProcessSelectedDelegate(std::shared_ptr<FTargetProcess> Target, std::shared_ptr<RTTI> RTTI);
	void OnClassSelectedDelegate(std::shared_ptr<_Class> InClass);
	void RenameFunction(std::pair<const uintptr_t, std::string>* InFunction);
	void CopyInfo();
	
	std::shared_ptr<_Class> SelectedClass;
	std::shared_ptr<FTargetProcess> Target;
	std::shared_ptr<RTTI> RTTIObserver;

	// popups
	std::shared_ptr<RenamePopup> RenamePopupWnd;
};