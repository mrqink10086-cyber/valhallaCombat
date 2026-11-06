#include "include/CombatTrace.h"

#if VC_COMBAT_TRACE

#include <Windows.h>

#include <array>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <string>
#include <vector>

namespace CombatTrace
{
	namespace
	{
		enum class Phase : std::uint8_t
		{
			Event,
			SpanBegin,
			SpanEnd
		};

		struct Event
		{
			std::uint64_t sequence{};
			std::uint64_t spanId{};
			std::uint64_t qpc{};
			std::uint64_t session{};
			std::uint64_t actorHandle{};
			std::uint64_t durationNs{};
			std::uint32_t threadId{};
			EventKind kind{ EventKind::CostHook };
			Phase phase{ Phase::Event };
			bool hasStamina{};
			double staminaBefore{};
			double staminaAfter{};
		};

		struct State
		{
			std::mutex mutex;
			std::array<Event, capacity> events{};
			std::uint32_t head{};
			std::uint32_t count{};
			std::uint64_t nextSequence{ 1 };
			std::uint64_t dropped{};
		};

		struct AppendResult
		{
			std::uint64_t sequence{};
			std::uint64_t qpc{};
		};

		State& state() noexcept
		{
			static State value;
			return value;
		}

		std::uint64_t qpcFrequency() noexcept
		{
			static const std::uint64_t frequency = [] {
				LARGE_INTEGER value{};
				return QueryPerformanceFrequency(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 1ULL;
			}();
			return frequency;
		}

		std::uint64_t readQpc() noexcept
		{
			LARGE_INTEGER value{};
			return QueryPerformanceCounter(&value) ? static_cast<std::uint64_t>(value.QuadPart) : 0ULL;
		}

		std::uint64_t toNanoseconds(std::uint64_t ticks) noexcept
		{
			const auto frequency = qpcFrequency();
			return (ticks / frequency) * 1000000000ULL + ((ticks % frequency) * 1000000000ULL) / frequency;
		}

		std::filesystem::path makeTracePath(const std::filesystem::path& outputDirectory)
		{
			static std::atomic<std::uint64_t> nextFile{ 1 };
			const auto qpc = readQpc();
			const auto process = GetCurrentProcessId();
			const auto fileIndex = nextFile.fetch_add(1);
			const auto fileName = std::string("combat_trace_") + std::to_string(process) + "_" +
				std::to_string(qpc) + "_" + std::to_string(fileIndex) + ".csv";
			return outputDirectory / fileName;
		}

		AppendResult append(Event event, std::uint64_t durationStartQpc = 0) noexcept
		{
			auto& buffer = state();
			std::lock_guard lock(buffer.mutex);
			event.sequence = buffer.nextSequence++;
			event.qpc = readQpc();
			event.threadId = GetCurrentThreadId();
			if (durationStartQpc != 0 && event.qpc >= durationStartQpc) {
				event.durationNs = toNanoseconds(event.qpc - durationStartQpc);
			}

			std::uint32_t slot{};
			if (buffer.count == capacity) {
				slot = buffer.head;
				buffer.head = (buffer.head + 1) % capacity;
				++buffer.dropped;
			} else {
				slot = (buffer.head + buffer.count) % capacity;
				++buffer.count;
			}
			buffer.events[slot] = event;
			return { event.sequence, event.qpc };
		}

		const char* kindName(EventKind kind) noexcept
		{
			switch (kind) {
			case EventKind::CostHook: return "CostHook";
			case EventKind::PreHit: return "PreHit";
			case EventKind::Hit: return "Hit";
			case EventKind::Stop: return "Stop";
			case EventKind::CombatUpdate: return "CombatUpdate";
			case EventKind::HitProcessing: return "HitProcessing";
			case EventKind::EngineMeleeHit: return "EngineMeleeHit";
			case EventKind::ProjectileCollision: return "ProjectileCollision";
			case EventKind::ProjectileApply: return "ProjectileApply";
			case EventKind::PrecisionPreHit: return "PrecisionPreHit";
			case EventKind::TimedBlock: return "TimedBlock";
			case EventKind::Tackle: return "Tackle";
			case EventKind::StunUpdate: return "StunUpdate";
			case EventKind::StunDamage: return "StunDamage";
			case EventKind::Execution: return "Execution";
			case EventKind::StaminaRestore: return "StaminaRestore";
			default: return "Unknown";
			}
		}
	}

	ScopedTimer::ScopedTimer(EventKind kind, std::uint64_t actorHandle, std::uint64_t session,
		bool hasStamina, double staminaBefore, double staminaAfter) noexcept :
		kind_(kind), actorHandle_(actorHandle), session_(session), startQpc_(0), hasStamina_(hasStamina),
		staminaBefore_(staminaBefore), staminaAfter_(staminaAfter)
	{
		Event event{};
		event.kind = kind_;
		event.phase = Phase::SpanBegin;
		event.actorHandle = actorHandle_;
		event.session = session_;
		event.hasStamina = hasStamina_;
		event.staminaBefore = staminaBefore_;
		event.staminaAfter = staminaAfter_;
		const auto result = append(event);
		spanId_ = result.sequence;
		startQpc_ = result.qpc;
	}

	ScopedTimer::~ScopedTimer() noexcept
	{
		Event event{};
		event.kind = kind_;
		event.phase = Phase::SpanEnd;
		event.spanId = spanId_;
		event.actorHandle = actorHandle_;
		event.session = session_;
		event.hasStamina = hasStamina_;
		event.staminaBefore = staminaBefore_;
		event.staminaAfter = staminaAfter_;
		append(event, startQpc_);
	}

	void record(EventKind kind, std::uint64_t actorHandle, std::uint64_t session,
		bool hasStamina, double staminaBefore, double staminaAfter) noexcept
	{
		Event event{};
		event.kind = kind;
		event.actorHandle = actorHandle;
		event.session = session;
		event.hasStamina = hasStamina;
		event.staminaBefore = staminaBefore;
		event.staminaAfter = staminaAfter;
		append(event);
	}

	bool flush(const std::filesystem::path& outputDirectory) noexcept
	{
		try {
			if (outputDirectory.empty() || !outputDirectory.is_absolute()) return false;
			std::vector<Event> snapshot;
			std::uint64_t dropped{};
			{
				auto& buffer = state();
				std::lock_guard lock(buffer.mutex);
				snapshot.reserve(buffer.count);
				for (std::uint32_t index = 0; index < buffer.count; ++index) {
					snapshot.push_back(buffer.events[(buffer.head + index) % capacity]);
				}
				dropped = buffer.dropped;
				buffer.head = 0;
				buffer.count = 0;
				buffer.dropped = 0;
			}

			std::filesystem::create_directories(outputDirectory);
			const auto filePath = makeTracePath(outputDirectory);
			std::ofstream output(filePath, std::ios::out | std::ios::trunc);
			if (!output) return false;
			output << "# qpc_frequency," << qpcFrequency() << "\n";
			output << "# events," << snapshot.size() << "\n";
			output << "# dropped_events," << dropped << "\n";
			output << "sequence,phase,kind,span_id,qpc,thread_id,session,actor_handle,duration_ns,has_stamina,stamina_before,stamina_after\n";
			output << std::setprecision(17);
			for (const auto& event : snapshot) {
				const auto phase = event.phase == Phase::SpanBegin ? "SpanBegin" : event.phase == Phase::SpanEnd ? "SpanEnd" : "Event";
				output << event.sequence << ',' << phase << ',' << kindName(event.kind) << ',' << event.spanId << ','
					<< event.qpc << ',' << event.threadId << ',' << event.session << ',' << event.actorHandle << ','
					<< event.durationNs << ',' << (event.hasStamina ? 1 : 0) << ',' << event.staminaBefore << ','
					<< event.staminaAfter << '\n';
			}
			output.flush();
			return output.good();
		} catch (...) {
			return false;
		}
	}

	std::uint32_t bufferedCount() noexcept
	{
		auto& buffer = state();
		std::lock_guard lock(buffer.mutex);
		return buffer.count;
	}

	std::uint64_t droppedCount() noexcept
	{
		auto& buffer = state();
		std::lock_guard lock(buffer.mutex);
		return buffer.dropped;
	}
}

#endif
