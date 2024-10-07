#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

// thread-safe blocking queue designed to pass data and success/failure states to another thread
template <class T>
class BlockingQueue
{
	std::queue<T> m_queue;
	std::mutex m_mutex;
	std::condition_variable m_cv;
	std::exception_ptr m_except;
	bool m_complete = false;

public:
	BlockingQueue() = default;
	BlockingQueue(const BlockingQueue&) = delete;
	BlockingQueue& operator=(const BlockingQueue&) = delete;

	template <class TVal>
	void push(TVal&& value)
	{
		{
			std::lock_guard lock(m_mutex);
			if (m_complete)
				return;
			m_queue.push(std::forward<TVal>(value));
		}
		m_cv.notify_one();
	}

	std::optional<T> take(std::stop_token token = {})
	{
		std::stop_callback callback(token, [this]()
			{
				{ std::lock_guard lock(m_mutex); }  // lock and unlock
				m_cv.notify_all();
			});

		// Define unique_lock AFTER stop_callback.
		// This makes sure that the lock is unlocked BEFORE stop_callback is destroyed.
		// Because the destructor of stop_callback will wait for the callback to complete,
		// which can cause deadlocks.
		std::unique_lock lock(m_mutex);

		while (!token.stop_requested() && m_queue.empty() && !m_complete)
			m_cv.wait(lock);

		if (m_except)
			std::rethrow_exception(m_except);
		if (token.stop_requested() || m_queue.empty())  // cancelled or completed
			return {};

		T val = std::move(m_queue.front());
		m_queue.pop();
		return val;
	}
	void complete()
	{
		std::lock_guard lock(m_mutex);
		if (m_complete)
			return;
		m_complete = true;
		if (m_queue.empty())
			m_cv.notify_all();
	}
	void fail(std::exception_ptr except)
	{
		std::lock_guard lock(m_mutex);
		if (m_complete)
			return;
		m_except = except;
		m_complete = true;
		m_cv.notify_all();
	}
};