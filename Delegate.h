#pragma once
#include <tuple>
#include <type_traits>
#include <vector>
#include <utility>
#include <memory>

#include <functional>
#include <vector>

template <typename... Args>
class Delegate
{
public:
	Delegate() = default;

	template <typename Callable>
	Delegate(Callable&& callable)
	{
		static_assert(std::is_invocable_v<Callable, Args...>,
			"Callable must be invocable with specified arguments");
		function_ = std::forward<Callable>(callable);
	}

	template <typename ThisClass, typename MemberFunction>
	Delegate(ThisClass* instance, MemberFunction&& member_function)
	{
		static_assert(std::is_invocable_v<MemberFunction, ThisClass*, Args...>,
			"Member function must be invocable with specified arguments");
		function_ = [instance, member_function](Args... args)
		{
			return (instance->*member_function)(std::forward<Args>(args)...);
		};
	}

	void operator()(Args... args) const
	{
		if (function_)
		{
			function_(std::forward<Args>(args)...);
		}
	}

	explicit operator bool() const { return static_cast<bool>(function_); }

private:
	std::function<void(Args...)> function_;
};

template <typename... Args>
class MulticastDelegate
{
public:
	MulticastDelegate() = default;

	template <typename Callable>
	void Bind(Callable&& callable)
	{
		delegates_.emplace_back(std::forward<Callable>(callable));
	}

	template <typename ThisClass, typename MemberFunction>
	void BindObject(ThisClass* instance, MemberFunction&& member_function)
	{
		delegates_.emplace_back(instance, std::forward<MemberFunction>(member_function));
	}

	void DestroyBindings()
	{
		delegates_.clear();
	}

	void operator()(Args... args) const
	{
		for (const auto& delegate : delegates_)
		{
			delegate(std::forward<Args>(args)...);
		}
	}

	explicit operator bool() const { return !delegates_.empty(); }

private:
	std::vector<Delegate<Args...>> delegates_;
};