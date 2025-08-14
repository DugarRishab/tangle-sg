// debug_lock.h
#pragma once
#include <mutex>
#include <chrono>
#include <iostream>
#include <thread>

template <typename M>
class TimedLock
{
public:
	TimedLock(M &m, const char *name, long warn_ms = 20)
		: m_(m), name_(name)
	{
		auto start = std::chrono::steady_clock::now();
		m_.lock();
		auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
						  std::chrono::steady_clock::now() - start)
						  .count();
		if (waited > warn_ms)
		{
			std::cerr << "[LOCK-WAIT] " << name_ << " waited " << waited
					  << "ms in thread " << std::this_thread::get_id() << std::endl;
		}
	}
	~TimedLock() { m_.unlock(); }

private:
	M &m_;
	const char *name_;
	TimedLock(const TimedLock &) = delete;
	TimedLock &operator=(const TimedLock &) = delete;
};
