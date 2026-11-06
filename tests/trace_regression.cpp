#include "CombatTrace.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	void require(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

#if VC_COMBAT_TRACE
	struct TraceFile
	{
		std::vector<std::string> rows;
		std::uint64_t eventCount{};
		std::uint64_t droppedCount{};
	};

	std::uint64_t parseHeaderValue(const std::string& line, const char* key)
	{
		const std::string prefix = std::string("# ") + key + ",";
		require(line.rfind(prefix, 0) == 0, "trace CSV metadata header is missing");
		return std::stoull(line.substr(prefix.size()));
	}

	std::vector<std::string> fields(const std::string& line)
	{
		std::vector<std::string> result;
		std::istringstream input(line);
		std::string field;
		while (std::getline(input, field, ',')) result.push_back(field);
		return result;
	}

	TraceFile readTrace(const std::filesystem::path& path)
	{
		std::ifstream input(path);
		require(input.good(), "trace CSV was not created");
		TraceFile result;
		std::string line;
		require(static_cast<bool>(std::getline(input, line)), "trace CSV has no frequency header");
		require(line.rfind("# qpc_frequency,", 0) == 0, "trace CSV QPC frequency is missing");
		require(static_cast<bool>(std::getline(input, line)), "trace CSV has no event count");
		result.eventCount = parseHeaderValue(line, "events");
		require(static_cast<bool>(std::getline(input, line)), "trace CSV has no dropped count");
		result.droppedCount = parseHeaderValue(line, "dropped_events");
		require(static_cast<bool>(std::getline(input, line)), "trace CSV has no column header");
		require(line == "sequence,phase,kind,span_id,qpc,thread_id,session,actor_handle,duration_ns,has_stamina,stamina_before,stamina_after",
			"trace CSV columns are unexpected");
		while (std::getline(input, line)) {
			if (!line.empty()) result.rows.push_back(line);
		}
		return result;
	}

	std::filesystem::path findTraceFile(const std::filesystem::path& directory)
	{
		for (const auto& entry : std::filesystem::directory_iterator(directory)) {
			if (entry.is_regular_file() && entry.path().extension() == ".csv") return entry.path();
		}
		throw std::runtime_error("trace CSV was not found in the output directory");
	}

	void runEnabledChecks()
	{
		require(CombatTrace::enabled, "trace-enabled build did not enable tracing");
		require(CombatTrace::capacity >= 65536, "trace ring capacity is too small");
		const auto directory = std::filesystem::absolute(std::filesystem::current_path() / "trace_regression_output");
		std::filesystem::remove_all(directory);

		CombatTrace::record(CombatTrace::EventKind::CostHook, 42, 7, true, 97.5, 91.25);
		CombatTrace::record(CombatTrace::EventKind::PreHit, 42, 7);
		CombatTrace::record(CombatTrace::EventKind::Hit, 42, 7);
		CombatTrace::record(CombatTrace::EventKind::Stop, 42, 7);
		{
			VC_TRACE_SCOPE(CombatTrace::EventKind::CombatUpdate, 42, 8);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}

		constexpr std::uint32_t threadCount = 4;
		constexpr std::uint32_t eventsPerThread = 1500;
		std::vector<std::thread> writers;
		writers.reserve(threadCount);
		for (std::uint32_t threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
			writers.emplace_back([threadIndex] {
				for (std::uint32_t eventIndex = 0; eventIndex < eventsPerThread; ++eventIndex) {
					const auto actor = 1000ULL + static_cast<std::uint64_t>(threadIndex) * eventsPerThread + eventIndex;
					CombatTrace::record(CombatTrace::EventKind::Hit, actor, 20 + threadIndex);
				}
			});
		}
		for (auto& writer : writers) writer.join();

		require(CombatTrace::bufferedCount() == 4 + 2 + threadCount * eventsPerThread,
			"trace buffer lost events before reaching capacity");
		require(CombatTrace::flush(directory), "trace flush failed");
		const auto csvPath = findTraceFile(directory);
		const auto trace = readTrace(csvPath);
		require(trace.eventCount == trace.rows.size(), "trace event count does not match CSV rows");
		require(trace.eventCount == 4 + 2 + threadCount * eventsPerThread, "trace flush omitted events");
		require(trace.droppedCount == 0, "trace reported drops before the ring filled");

		const std::vector<std::string> expectedOrder{ "CostHook", "PreHit", "Hit", "Stop" };
		std::size_t orderIndex{};
		std::uint64_t previousSequence{};
		std::set<std::uint64_t> sequences;
		bool foundSample{};
		bool foundTimedSpan{};
		for (const auto& row : trace.rows) {
			const auto rowFields = fields(row);
			require(rowFields.size() == 12, "trace row has an unexpected column count");
			const auto sequence = std::stoull(rowFields[0]);
			require(sequence > previousSequence, "trace rows are not in event order");
			previousSequence = sequence;
			require(sequences.insert(sequence).second, "trace sequence id was duplicated");
			require(std::stoull(rowFields[4]) != 0, "trace row has no QPC timestamp");
			require(std::stoul(rowFields[5]) != 0, "trace row has no thread id");
			if (rowFields[7] == "42" && rowFields[1] == "Event" && orderIndex < expectedOrder.size()) {
				require(rowFields[2] == expectedOrder[orderIndex], "combat event sequence changed");
				++orderIndex;
			}
			if (rowFields[2] == "CostHook" && rowFields[7] == "42") {
				foundSample = rowFields[9] == "1" && rowFields[10] == "97.5" && rowFields[11] == "91.25";
			}
			if (rowFields[2] == "CombatUpdate" && rowFields[1] == "SpanEnd") {
				foundTimedSpan = std::stoull(rowFields[8]) > 0 && std::stoull(rowFields[3]) != 0;
			}
		}
		require(orderIndex == expectedOrder.size(), "one or more combat events were not recorded");
		require(sequences.size() == trace.eventCount, "trace sequence ids are incomplete");
		require(foundSample, "caller-supplied stamina values were not written");
		require(foundTimedSpan, "RAII timer did not write a duration span");
		require(CombatTrace::bufferedCount() == 0 && CombatTrace::droppedCount() == 0,
			"flush did not clear the captured batch");

		std::filesystem::remove(csvPath);
		constexpr std::uint32_t overflowEvents = CombatTrace::capacity + 19;
		for (std::uint32_t index = 0; index < overflowEvents; ++index) {
			CombatTrace::record(CombatTrace::EventKind::PreHit, index, 99);
		}
		require(CombatTrace::bufferedCount() == CombatTrace::capacity, "trace ring did not stay bounded");
		require(CombatTrace::droppedCount() == 19, "trace ring did not count overwritten events");
		require(CombatTrace::flush(directory), "trace overflow flush failed");
		const auto overflowTrace = readTrace(findTraceFile(directory));
		require(overflowTrace.eventCount == CombatTrace::capacity, "trace overflow flush had the wrong event count");
		require(overflowTrace.rows.size() == CombatTrace::capacity, "trace overflow CSV row count is wrong");
		require(overflowTrace.droppedCount == 19, "trace overflow count was not persisted");
		std::filesystem::remove_all(directory);
	}
#else
	std::uint64_t sideEffectCount{};

	std::uint64_t sideEffect() noexcept
	{
		return ++sideEffectCount;
	}

	void runDisabledChecks()
	{
		require(!CombatTrace::enabled, "default build unexpectedly enabled tracing");
		const auto directory = std::filesystem::absolute(std::filesystem::current_path() / "trace_regression_disabled_output");
		std::filesystem::remove_all(directory);
		VC_TRACE_EVENT(static_cast<CombatTrace::EventKind>(sideEffect()), sideEffect(), sideEffect(), true, sideEffect(), sideEffect());
		VC_TRACE_SCOPE(static_cast<CombatTrace::EventKind>(sideEffect()), sideEffect(), sideEffect());
		require(sideEffectCount == 0, "disabled trace macros evaluated their arguments");
		CombatTrace::record(static_cast<CombatTrace::EventKind>(sideEffect()), sideEffect(), sideEffect());
		require(sideEffectCount == 3, "disabled inline API changed normal argument evaluation");
		require(!CombatTrace::flush(directory), "disabled trace flush unexpectedly wrote a file");
		require(!std::filesystem::exists(directory), "disabled trace created an output directory");
	}
#endif
}

int main()
{
	try {
#if VC_COMBAT_TRACE
		runEnabledChecks();
#else
		runDisabledChecks();
#endif
		return 0;
	} catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
