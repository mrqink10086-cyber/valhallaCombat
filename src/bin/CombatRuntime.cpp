#include "include/CombatRuntime.h"
#include "include/offsets.h"
#include <algorithm>
#include <cmath>

CombatRuntime& CombatRuntime::get() { static CombatRuntime runtime; return runtime; }
void CombatRuntime::initialize() { owner = GetCurrentThreadId(); last = std::chrono::steady_clock::now(); }
bool CombatRuntime::ownerThread() const { return owner.load() == GetCurrentThreadId(); }
bool CombatRuntime::ready() const { return active.load() && ownerThread(); }
void CombatRuntime::beginSession() { initialize(); active = true; }
void CombatRuntime::endSession()
{
	active = false;
	++epoch;
	{
		std::lock_guard lock(mutex);
		work.clear();
		clock = 0;
	}
	if (ownerThread()) clearSlowTime();
}
CombatRuntime::Ticket CombatRuntime::schedule(float seconds, Task task)
{
	std::lock_guard lock(mutex);
	if (!active || !std::isfinite(seconds)) return 0;
	const auto id = nextTicket++;
	work.push_back({id, epoch.load(), clock + std::max(0.0f, seconds), std::move(task)});
	return id;
}
void CombatRuntime::cancel(Ticket ticket)
{
	std::lock_guard lock(mutex);
	std::erase_if(work, [ticket](const Work& entry) { return entry.id == ticket; });
}
void CombatRuntime::control(Task task)
{
	const auto generation = epoch.load();
	if (auto tasks = SKSE::GetTaskInterface()) {
		tasks->AddTask([this, generation, task = std::move(task)] {
			if (generation != epoch.load()) return;
			try { task(); } catch (const std::exception& error) { logger::error("Control command failed: {}", error.what()); }
		});
	}
}
void CombatRuntime::update(bool paused)
{
	if (!ownerThread()) return;
	const auto now = std::chrono::steady_clock::now();
	const auto elapsed = std::chrono::duration<double>(now - last).count();
	last = now;
	realDelta=0;
	if (!active || paused) return;
	realDelta=static_cast<float>(elapsed);
	std::vector<Ticket> due;
	{
		std::lock_guard lock(mutex);
		clock += elapsed;
		for (auto it = work.begin(); it != work.end();) {
			if (it->session != epoch.load()) it = work.erase(it);
			else if (it->deadline <= clock) { due.push_back(it->id); ++it; }
			else ++it;
		}
	}
	for (const auto id : due) {
		if (!ready()) break;
		Task task;
		{ std::lock_guard lock(mutex);
		 const auto it=std::find_if(work.begin(),work.end(),[id](const Work& item){ return item.id==id; });
		 if (it==work.end()) continue;
		 if (it->session==epoch.load()) task=std::move(it->task);
		 work.erase(it);
		}
		try { if (task) task(); } catch (const std::exception& error) { logger::error("Combat request {} failed: {}", id, error.what()); }
	}
	updateSlowTime();
}
void CombatRuntime::slowTime(float seconds, float multiplier)
{
	if (!ready() || !std::isfinite(seconds) || !std::isfinite(multiplier) || seconds <= 0) return;
	if (!ownsSpeed) { originalSpeed = RE::Offset::getSGTM(); writtenSpeed = originalSpeed; ownsSpeed = true; }
	slows.push_back({clock + seconds, std::clamp(multiplier, 0.01f, 1.0f)});
	updateSlowTime();
}
void CombatRuntime::updateSlowTime()
{
	if (!ownsSpeed) return;
	if (std::abs(RE::Offset::getSGTM() - writtenSpeed) > 0.0001f) {
		slows.clear(); ownsSpeed = false; return;
	}
	std::erase_if(slows, [this](const Slow& effect) { return effect.deadline <= clock; });
	if (slows.empty()) { clearSlowTime(); return; }
	writtenSpeed = originalSpeed;
	for (const auto& effect : slows) writtenSpeed = std::min(writtenSpeed, effect.multiplier);
	RE::Offset::SGTM(writtenSpeed);
}
void CombatRuntime::clearSlowTime()
{
	if (ownsSpeed && std::abs(RE::Offset::getSGTM() - writtenSpeed) <= 0.0001f) RE::Offset::SGTM(originalSpeed);
	slows.clear(); ownsSpeed = false;
}
