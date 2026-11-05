#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>
#include <cstdint>
#include <Windows.h>

class CombatRuntime
{
public:
	using Task = std::function<void()>;
	using Ticket = std::uint64_t;
	static CombatRuntime& get();
	void initialize();
	void beginSession();
	void endSession();
	bool ready() const;
	bool ownerThread() const;
	void update(bool paused);
		void resetClock() { last=std::chrono::steady_clock::now(); realDelta=0; }
		float activeDelta() const { return realDelta; }
	Ticket schedule(float seconds, Task task);
	void cancel(Ticket ticket);
	void control(Task task);
	void slowTime(float seconds, float multiplier);
	void clearSlowTime();
	std::uint64_t session() const { return epoch.load(); }

private:
	struct Work { Ticket id; std::uint64_t session; double deadline; Task task; };
	struct Slow { double deadline; float multiplier; };
	std::atomic<std::uint64_t> epoch{1};
	std::atomic<bool> active{false};
	std::atomic<DWORD> owner{0};
	std::mutex mutex;
	std::vector<Work> work;
	Ticket nextTicket{1};
	double clock{0};
	std::chrono::steady_clock::time_point last{};
	std::vector<Slow> slows;
	float originalSpeed{1};
	float writtenSpeed{1};
	bool ownsSpeed{false};
		float realDelta{0};
	void updateSlowTime();
};
