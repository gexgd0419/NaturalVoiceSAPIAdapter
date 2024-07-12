#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tuple>
#include <mutex>
#include <system_error>
#include <unordered_map>
#include <future>


// A task scheduler based on timer queue,
// so that we can wait for unfinished tasks and cancel unexecuted tasks
// before this DLL is unloaded.
class TaskScheduler
{
private:
	// Timer queues CANNOT be created in DllMain, otherwise deadlocks would happen on Windows XP
	// So we create the timer queue on first use
	HANDLE hTimerQueue = nullptr;

public:
	void Initialize()
	{
		if (!hTimerQueue)
		{
			hTimerQueue = CreateTimerQueue();
			if (!hTimerQueue)
				throw std::system_error(GetLastError(), std::system_category());
		}
	}

	~TaskScheduler()
	{
		Uninitialize(true);
	}

	void Uninitialize(bool waitForTasks = true)
	{
		if (!hTimerQueue)
			return;

		(void)DeleteTimerQueueEx(hTimerQueue, waitForTasks ? INVALID_HANDLE_VALUE : nullptr);
		hTimerQueue = nullptr;

		// delete tuples that haven't been deleted by callback functions
		std::lock_guard lock(deleterMutex);
		for (auto& [pTuple, pFunc] : deleters)
		{
			pFunc(pTuple);
		}
	}

private:
	template <class Tuple>
	struct ParamData  // store the parent scheduler, and a tuple to pass to std::invoke
	{
		TaskScheduler* pScheduler;
		Tuple tuple;
		template <class... Args>
		ParamData(TaskScheduler* pScheduler, Args&&... args)
			: pScheduler(pScheduler),
			tuple(std::forward<Args>(args)...)
		{}
	};

	typedef void (*DataDeleterFunc)(PVOID);
	std::unordered_map<PVOID, DataDeleterFunc> deleters;
	std::mutex deleterMutex;

	template <class DataType, size_t... Indices>
	static void CALLBACK TimerQueueProc(PVOID param, BOOLEAN)
	{
		std::unique_ptr<DataType> pData(static_cast<DataType*>(param));

		{
			// this callback function has taken the responsibility to delete the tuple
			// so remove it from the deleter list
			TaskScheduler& scheduler = *pData->pScheduler;
			std::lock_guard lock(scheduler.deleterMutex);
			scheduler.deleters.erase(param);
		}

		auto& tup = pData->tuple;
		std::invoke(std::move(std::get<Indices>(tup))...);
	}

	template <class DataType, size_t... Indices>
	static auto GetTimerQueueProc(std::index_sequence<Indices...>) noexcept
	{
		return &TimerQueueProc<DataType, Indices...>;
	}

	template <class DataType>
	static void DataDeleter(PVOID pData)
	{
		delete static_cast<DataType*>(pData);
	}

public:
	template <class Func, class... Args> requires std::invocable<Func, Args...>
	HANDLE StartNewTask(DWORD delayMs, Func&& func, Args&&... args)
	{
		Initialize();
		using Tuple = std::tuple<std::decay_t<Func>, std::decay_t<Args>...>;
		using DataType = ParamData<Tuple>;
		auto pData = std::make_unique<DataType>(
			this,
			std::forward<Func>(func), std::forward<Args>(args)...
		);

		HANDLE hTimer;
		if (!CreateTimerQueueTimer(&hTimer, hTimerQueue,
			GetTimerQueueProc<DataType>(std::make_index_sequence<1 + sizeof...(Args)>()),
			pData.get(),
			delayMs, 0,
			WT_EXECUTEONLYONCE | WT_EXECUTELONGFUNCTION))
		{
			throw std::system_error(GetLastError(), std::system_category());
		}

		std::lock_guard lock(deleterMutex);
		deleters.emplace(pData.get(), &DataDeleter<DataType>);
		pData.release();

		return hTimer;
	}

	template <class Func, class... Args> requires std::invocable<Func, Args...>
	HANDLE StartNewTask(Func&& func, Args&&... args)
	{
		return StartNewTask(0, std::forward<Func>(func), std::forward<Args>(args)...);
	}

	void CancelTask(HANDLE hTask, bool waitForTask)
	{
		(void)DeleteTimerQueueTimer(hTimerQueue, hTask, waitForTask ? INVALID_HANDLE_VALUE : nullptr);
	}
};