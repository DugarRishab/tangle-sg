// debug_lock.h
#pragma once
#include <mutex>
#include <chrono>
#include <thread>
#include <iostream>
#include <atomic>
#include <sstream>

struct LockStats
{
	// optional global counter for debug
	static std::atomic<int> total_locks;
};
std::atomic<int> LockStats::total_locks{0};

template <typename Mutex>
class DebugScopedLock
{
public:
	DebugScopedLock(Mutex &m, const char *name, long warn_ms = 10)
		: m_(m), name_(name), warn_ms_(warn_ms)
	{
		auto tid = std::this_thread::get_id();
		auto t0 = std::chrono::steady_clock::now();
		m_.lock();
		auto t1 = std::chrono::steady_clock::now();
		waited_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
		owner_acq_time_ = t1;
		LockStats::total_locks.fetch_add(1, std::memory_order_relaxed);

		std::ostringstream ss;
		ss << "[LOCK-ACQ] " << name_ << " by thread " << tid
		   << " waited " << waited_ms_ << " ms\n";
		std::cerr << ss.str() << std::flush;
	}

	~DebugScopedLock()
	{
		auto tid = std::this_thread::get_id();
		auto t2 = std::chrono::steady_clock::now();
		auto held_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - owner_acq_time_).count();

		std::ostringstream ss;
		ss << "[LOCK-REL] " << name_ << " by thread " << tid
		   << " held " << held_ms << " ms\n";
		std::cerr << ss.str() << std::flush;

		m_.unlock();
	}

	// disable copy
	DebugScopedLock(const DebugScopedLock &) = delete;
	DebugScopedLock &operator=(const DebugScopedLock &) = delete;

private:
	Mutex &m_;
	const char *name_;
	long warn_ms_;
	std::chrono::steady_clock::time_point owner_acq_time_;
	long waited_ms_;
};
