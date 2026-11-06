#include "perf_common.h"
#include "include/CombatRuntime.h"

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace {
constexpr std::size_t targetOperations = 256;
constexpr std::size_t idleFrames = 8;

std::size_t repetitionsFor(std::size_t n)
{
	return std::max<std::size_t>(1, (targetOperations + n - 1) / n);
}

struct RuntimeBatch {
	std::vector<std::unique_ptr<CombatRuntime>> runtimes;
	std::vector<std::vector<CombatRuntime::Ticket>> tickets;

	explicit RuntimeBatch(std::size_t count) : tickets(count)
	{
		runtimes.reserve(count);
		for (std::size_t i = 0; i < count; ++i) {
			runtimes.push_back(std::make_unique<CombatRuntime>());
		}
	}

	void begin()
	{
		for (auto& runtime : runtimes) runtime->beginSession();
	}

	void end()
	{
		for (auto& runtime : runtimes) runtime->endSession();
	}

	void cancelAll()
	{
		for (std::size_t i = 0; i < runtimes.size(); ++i) {
			for (const auto ticket : tickets[i]) runtimes[i]->cancel(ticket);
			tickets[i].clear();
		}
	}
};

void scheduleTasks(RuntimeBatch& batch, std::size_t n, float delay, const CombatRuntime::Task& callback)
{
	for (std::size_t i = 0; i < batch.runtimes.size(); ++i) {
		auto& tickets = batch.tickets[i];
		for (std::size_t j = 0; j < n; ++j) tickets.push_back(batch.runtimes[i]->schedule(delay, callback));
	}
}
}

int main(int argc, char** argv)
{
	const auto samples = perf::sampleCount(argc, argv);
	constexpr std::array<std::size_t, 4> sizes{ 1, 32, 256, 4096 };
	perf::printHeader();
	std::uint64_t callbackCount = 0;
	auto callback = [&callbackCount] { ++callbackCount; };
	auto threeCallbacks = [&callbackCount] { callbackCount += 3; };

	for (const auto n : sizes) {
		const auto repetitions = repetitionsFor(n);
		const auto totalTasks = repetitions * n;

		{
			RuntimeBatch batch(repetitions);
			perf::print(perf::measurePrepared("schedule", n, totalTasks, samples,
				[&] { batch.begin(); },
				[&] { scheduleTasks(batch, n, 60.0f, callback); },
				[&] { batch.cancelAll(); batch.end(); }));
		}

		{
			RuntimeBatch batch(repetitions);
			perf::print(perf::measurePrepared("cancel", n, totalTasks, samples,
				[&] { batch.begin(); scheduleTasks(batch, n, 60.0f, callback); },
				[&] { batch.cancelAll(); },
				[&] { batch.end(); }));
		}

		{
			RuntimeBatch batch(repetitions);
			perf::print(perf::measurePrepared("idle_update", n, totalTasks, samples,
				[&] { batch.begin(); },
				[&] { for (auto& runtime : batch.runtimes) for (std::size_t i = 0; i < n; ++i) runtime->update(false); },
				[&] { batch.end(); }));
		}

		{
			RuntimeBatch batch(repetitions);
			perf::print(perf::measurePrepared("idle_with_pending_frames", n, repetitions * idleFrames, samples,
				[&] { batch.begin(); scheduleTasks(batch, n, 60.0f, callback); },
				[&] { for (auto& runtime : batch.runtimes) for (std::size_t i = 0; i < idleFrames; ++i) runtime->update(false); },
				[&] { batch.cancelAll(); batch.end(); }));
		}

		{
			RuntimeBatch batch(repetitions);
			perf::print(perf::measurePrepared("due_batch", n, totalTasks, samples,
				[&] { batch.begin(); scheduleTasks(batch, n, 0.0f, callback); },
				[&] { for (auto& runtime : batch.runtimes) runtime->update(false); },
				[&] { batch.end(); }));
		}

		{
			RuntimeBatch batch(repetitions);
			perf::print(perf::measurePrepared("projectile_three_tasks", n, totalTasks, samples,
				[&] { batch.begin(); scheduleTasks(batch, n * 3, 0.0f, callback); },
				[&] { for (auto& runtime : batch.runtimes) runtime->update(false); },
				[&] { batch.end(); }));
		}

		{
			RuntimeBatch batch(repetitions);
			perf::print(perf::measurePrepared("projectile_one_task", n, totalTasks, samples,
				[&] { batch.begin(); scheduleTasks(batch, n, 0.0f, threeCallbacks); },
				[&] { for (auto& runtime : batch.runtimes) runtime->update(false); },
				[&] { batch.end(); }));
		}
	}
	return callbackCount == 0 ? 1 : 0;
}
