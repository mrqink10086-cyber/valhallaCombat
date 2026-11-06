#include "include/CombatSelfTest.h"

#if VC_COMBAT_TRACE

#include "include/CombatRuntime.h"
#include "include/CombatTrace.h"
#include "include/Utils.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
	using Clock = std::chrono::steady_clock;

	std::uint64_t elapsedNs(Clock::time_point start, Clock::time_point end) noexcept
	{
		return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
	}

	void writeCsvRow(std::ofstream& csv, std::string_view suite, std::string_view test,
		std::string_view domain, std::size_t batch, std::size_t operations,
		std::uint64_t nanoseconds, std::string_view status, std::string_view details)
	{
		const double perOperation = operations == 0 ? 0.0 : static_cast<double>(nanoseconds) / static_cast<double>(operations);
		csv << suite << ',' << test << ',' << domain << ',' << batch << ',' << operations << ','
			<< nanoseconds << ',' << std::setprecision(9) << perOperation << ',' << status << ',' << details << '\n';
	}

	struct MatrixCheck
	{
		bool finite{};
		bool orthonormal{};
		float maxError{};
	};

	MatrixCheck checkRotationMatrix(const RE::NiMatrix3& matrix) noexcept
	{
		for (const auto& row : matrix.entry) {
			for (const float value : row) {
				if (!std::isfinite(value)) return { false, false, std::numeric_limits<float>::infinity() };
			}
		}

		float maxError = 0.0f;
		for (std::size_t rowA = 0; rowA < 3; ++rowA) {
			for (std::size_t rowB = 0; rowB < 3; ++rowB) {
				float dot = 0.0f;
				for (std::size_t column = 0; column < 3; ++column) {
					dot += matrix.entry[rowA][column] * matrix.entry[rowB][column];
				}
				const float expected = rowA == rowB ? 1.0f : 0.0f;
				maxError = std::max(maxError, std::abs(dot - expected));
			}
		}
		return { true, maxError <= 1.0e-4f, maxError };
	}

	void writeSkippedRow(std::ofstream& csv, std::string_view test, std::string_view reason)
	{
		writeCsvRow(csv, "engine_query", test, "actual_engine_query", 0, 0, 0, reason, "no_actor_mutation");
	}

#if defined(_MSC_VER)
	__declspec(noinline)
#else
	__attribute__((noinline))
#endif
	void measuredRotationMatrixCall(RE::NiMatrix3& matrix, float sacb, float cacb, float sb, volatile float& observableSink) noexcept
	{
		inlineUtils::SetRotationMatrix(matrix, sacb, cacb, sb);
		float checksum = 0.0f;
		for (const auto& row : matrix.entry) {
			for (const float value : row) checksum += value;
		}
		observableSink = checksum;
	}
}

namespace CombatSelfTest
{
	void startManualTrigger(const std::filesystem::path& outputDir) noexcept
	{
		if (outputDir.empty() || !outputDir.is_absolute()) return;
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) return;
		try {
			static std::jthread watcher;
			if (watcher.joinable()) return;
			watcher = std::jthread([outputDir, tasks](std::stop_token stop) {
				try {
					const auto request = outputDir / "combat_self_test.request";
					while (!stop.stop_requested()) {
						std::error_code error;
						if (std::filesystem::remove(request, error)) {
							tasks->AddTask([outputDir] {
								runMainMenu(outputDir);
								CombatTrace::flush(outputDir);
							});
						}
						std::this_thread::sleep_for(std::chrono::milliseconds(250));
					}
				} catch (const std::exception& error) {
					logger::error("Manual self-test trigger failed: {}", error.what());
				} catch (...) {
					logger::error("Manual self-test trigger failed with an unknown error");
				}
			});
			logger::info("Manual main-menu self-test waiting for {}", (outputDir / "combat_self_test.request").string());
		} catch (const std::exception& error) {
			logger::error("Cannot start manual self-test trigger: {}", error.what());
		} catch (...) {
			logger::error("Cannot start manual self-test trigger");
		}
	}

	void runMainMenu(const std::filesystem::path& outputDir) noexcept
	{
		const auto& globalRuntime = CombatRuntime::get();
		if (!globalRuntime.ownerThread()) {
			logger::warn("Main-menu self-test rejected: task thread {} is not the runtime owner", GetCurrentThreadId());
			return;
		}
		if (outputDir.empty() || !outputDir.is_absolute()) return;
		auto* ui = RE::UI::GetSingleton();
		if (!ui || !ui->IsMenuOpen(RE::MainMenu::MENU_NAME)) {
			logger::warn("Main-menu self-test rejected: MainMenu is not open");
			return;
		}

		static std::atomic<bool> hasRun{ false };
		bool expected = false;
		if (!hasRun.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
		logger::info("Main-menu self-test starting on thread {}", GetCurrentThreadId());

		try {
			const auto& targetDirectory = outputDir;
			std::filesystem::create_directories(targetDirectory);
			std::ofstream csv(targetDirectory / "combat_self_test.csv", std::ios::out | std::ios::trunc);
			if (!csv) throw std::runtime_error("cannot open combat_self_test.csv");
			csv << "suite,test,domain,batch,operations,elapsed_ns,ns_per_operation,status,details\n";

			bool allCoreChecksPassed = true;
			std::ostringstream summary;
			summary << "ValhallaCombat main-menu self-test\n"
				<< "scope=main_menu_safe\n"
				<< "combat_latency_claim=false\n"
				<< "production_cpu_core=local CombatRuntime scheduler and inlineUtils::SetRotationMatrix only\n"
				<< "engine_queries=read_only main-menu observations\n";

			// Exercise the real scheduler implementation on an isolated runtime. No global session or
			// game-time control is touched; this only measures production scheduler bookkeeping.
			{
				CombatRuntime probe;
				probe.beginSession();
				int ran = 0;
				int cancelledRan = 0;
				const auto cancelled = probe.schedule(0.0f, [&] { ++cancelledRan; });
				const auto immediate = probe.schedule(0.0f, [&] { ++ran; });
				probe.cancel(cancelled);
				probe.update(false);
				const bool schedulerCheckPassed = cancelled != 0 && immediate != 0 && ran == 1 && cancelledRan == 0;
				allCoreChecksPassed = allCoreChecksPassed && schedulerCheckPassed;
				writeCsvRow(csv, "runtime", "schedule_cancel_update_check", "production_cpu_core", 0, 1, 0,
					schedulerCheckPassed ? "passed" : "failed", "ran=1;cancelled_callback=0");

				constexpr std::size_t batchCount = 6;
				constexpr std::size_t iterationsPerBatch = 16;
				std::size_t timedCallbacks = 0;
				std::size_t cancelledCallbacks = 0;
				for (std::size_t batch = 0; batch < batchCount; ++batch) {
					const auto start = Clock::now();
					std::size_t completed = 0;
					for (; completed < iterationsPerBatch; ++completed) {
						const auto runTicket = probe.schedule(0.0f, [&] { ++timedCallbacks; });
						const auto cancelTicket = probe.schedule(10.0f, [&] { ++cancelledCallbacks; });
						if (runTicket == 0 || cancelTicket == 0) {
							allCoreChecksPassed = false;
							break;
						}
						probe.cancel(cancelTicket);
						probe.update(false);
					}
					const auto duration = elapsedNs(start, Clock::now());
					const bool batchPassed = completed == iterationsPerBatch && timedCallbacks == (batch + 1) * iterationsPerBatch && cancelledCallbacks == 0;
					allCoreChecksPassed = allCoreChecksPassed && batchPassed;
					writeCsvRow(csv, "runtime", "schedule_cancel_update_batch", "production_cpu_core", batch,
						completed, duration, batchPassed ? "passed" : "failed",
						"one_immediate_task;one_cancelled_task;owner_update");
				}
				probe.endSession();
				summary << "runtime_check=" << (schedulerCheckPassed ? "passed" : "failed")
					<< ";timed_callbacks=" << timedCallbacks << ";cancelled_callbacks=" << cancelledCallbacks << '\n';
			}

			// Time and validate the production matrix helper with vertical and general inputs.
			{
				struct MatrixCase { std::string_view name; float sacb; float cacb; float sb; bool vertical; };
				constexpr std::array<MatrixCase, 3> cases{{
					{ "vertical_positive", 0.0f, 0.0f, 1.0f, true },
					{ "vertical_negative", 0.0f, 0.0f, -1.0f, true },
					{ "general", 0.48f, 0.64f, 0.60f, false }
				}};
				constexpr std::size_t batchCount = 4;
				constexpr std::size_t iterationsPerBatch = 4096;
				bool matrixChecksPassed = true;
				volatile float observableSink = 0.0f;
				for (const auto& testCase : cases) {
					RE::NiMatrix3 matrix;
					inlineUtils::SetRotationMatrix(matrix, testCase.sacb, testCase.cacb, testCase.sb);
					const auto check = checkRotationMatrix(matrix);
					matrixChecksPassed = matrixChecksPassed && check.finite && check.orthonormal;
					for (std::size_t batch = 0; batch < batchCount; ++batch) {
						const auto start = Clock::now();
						for (std::size_t i = 0; i < iterationsPerBatch; ++i) {
							const float perturbation = static_cast<float>((i & 15U) + 1U) * 1.0e-7f;
							const float sacb = testCase.vertical ? 0.0f : testCase.sacb + perturbation;
							const float cacb = testCase.vertical ? 0.0f : testCase.cacb - perturbation * 0.5f;
							const float sb = testCase.vertical ? testCase.sb + std::copysign(perturbation, testCase.sb) : testCase.sb;
							measuredRotationMatrixCall(matrix, sacb, cacb, sb, observableSink);
						}
						const auto duration = elapsedNs(start, Clock::now());
						const auto batchCheck = checkRotationMatrix(matrix);
						const bool batchPassed = check.finite && check.orthonormal && batchCheck.finite && batchCheck.orthonormal;
						matrixChecksPassed = matrixChecksPassed && batchPassed;
						std::ostringstream details;
						details << "finite=" << (batchCheck.finite ? "true" : "false")
							<< ";orthonormal=" << (batchCheck.orthonormal ? "true" : "false")
							<< ";max_dot_error=" << std::setprecision(7) << batchCheck.maxError
							<< ";includes_noinline_wrapper_and_volatile_sink";
						writeCsvRow(csv, "matrix", testCase.name, "production_cpu_core", batch,
							iterationsPerBatch, duration, batchPassed ? "passed" : "failed", details.str());
					}
				}
				allCoreChecksPassed = allCoreChecksPassed && matrixChecksPassed;
				summary << "rotation_matrix_checks=" << (matrixChecksPassed ? "passed" : "failed") << '\n';
			}

			// These are real, read-only engine calls performed only after the owner/MainMenu gate.
			{
				constexpr std::size_t formLookupCount = 16;
				for (const auto formID : { RE::FormID{ 0x14 }, RE::FormID{ 0x7 } }) {
					std::size_t foundCount = 0;
					const auto start = Clock::now();
					for (std::size_t i = 0; i < formLookupCount; ++i) {
						if (RE::TESForm::LookupByID(formID)) ++foundCount;
					}
					const auto duration = elapsedNs(start, Clock::now());
					std::ostringstream name;
					name << "form_lookup_0x" << std::hex << formID;
					std::ostringstream details;
					details << "found_count=" << std::dec << foundCount << ";no_form_dereference";
					writeCsvRow(csv, "engine_query", name.str(), "actual_engine_query", 0,
						formLookupCount, duration, foundCount ? "found" : "absent", details.str());
				}

				auto* player = RE::PlayerCharacter::GetSingleton();
				if (!player) {
					writeSkippedRow(csv, "player_handle_get", "skipped_player_missing");
					writeSkippedRow(csv, "player_actor_values", "skipped_player_missing");
					summary << "player_queries=skipped_player_missing\n";
				} else {
					constexpr std::size_t handleGetCount = 16;
					std::size_t resolvedCount = 0;
					RE::ActorHandle playerHandle;
					RE::NiPointer<RE::Actor> playerRef;
					const auto start = Clock::now();
					for (std::size_t i = 0; i < handleGetCount; ++i) {
						playerHandle = player->GetHandle();
						auto resolved = playerHandle.get();
						if (resolved) {
							++resolvedCount;
							if (!playerRef) playerRef = std::move(resolved);
						}
					}
					const auto duration = elapsedNs(start, Clock::now());
					writeCsvRow(csv, "engine_query", "player_handle_get", "actual_engine_query", 0,
						handleGetCount, duration, resolvedCount ? "resolved" : "unresolved",
						"GetHandle_then_handle_get;no_actor_mutation");

					if (!playerRef) {
					writeSkippedRow(csv, "player_actor_values", "skipped_handle_unresolved");
					 summary << "player_queries=handle_unresolved\n";
					} else if (auto* avOwner = playerRef->AsActorValueOwner()) {
						constexpr std::size_t avReadCount = 32;
						double currentSum = 0.0;
						double permanentSum = 0.0;
						const auto avStart = Clock::now();
						for (std::size_t i = 0; i < avReadCount; ++i) {
							currentSum += avOwner->GetActorValue(RE::ActorValue::kStamina);
							permanentSum += avOwner->GetPermanentActorValue(RE::ActorValue::kStamina);
						}
						const auto avDuration = elapsedNs(avStart, Clock::now());
						const bool finiteValues = std::isfinite(currentSum) && std::isfinite(permanentSum);
						std::ostringstream details;
						details << std::setprecision(9) << "current_sum=" << currentSum << ";permanent_sum=" << permanentSum << ";no_actor_mutation";
						writeCsvRow(csv, "engine_query", "player_stamina_current_and_permanent", "actual_engine_query", 0,
							avReadCount * 2, avDuration, finiteValues ? "passed" : "nonfinite_value", details.str());
						summary << "player_queries=resolved;actor_value_owner=valid;stamina_values="
							<< (finiteValues ? "finite" : "nonfinite") << '\n';
					} else {
						writeSkippedRow(csv, "player_actor_values", "skipped_av_owner_missing");
						summary << "player_queries=resolved;actor_value_owner=missing\n";
					}
				}
			}

			writeCsvRow(csv, "combat_path", "melee_hit", "actual_engine_query", 0, 0, 0,
				"not_run_main_menu", "no_hit_pipeline_invocation");
			writeCsvRow(csv, "combat_path", "projectile_collision", "actual_engine_query", 0, 0, 0,
				"not_run_main_menu", "no_collision_or_havok_invocation");
			csv.flush();
			if (!csv) throw std::runtime_error("failed writing combat_self_test.csv");

			summary << "core_checks=" << (allCoreChecksPassed ? "passed" : "failed") << '\n'
				<< "melee_hit=not_run_main_menu\n"
				<< "projectile_collision=not_run_main_menu\n"
				<< "timings_are_not_real_combat_latency=true\n";
			std::ofstream summaryFile(targetDirectory / "combat_self_test_summary.txt", std::ios::out | std::ios::trunc);
			if (!summaryFile) throw std::runtime_error("cannot open combat_self_test_summary.txt");
			summaryFile << summary.str();
			summaryFile.flush();
			logger::info("Main-menu self-test finished: core_checks={}", allCoreChecksPassed ? "passed" : "failed");
		} catch (const std::exception& error) {
			try {
				const auto& targetDirectory = outputDir;
				std::filesystem::create_directories(targetDirectory);
				std::ofstream summary(targetDirectory / "combat_self_test_summary.txt", std::ios::out | std::ios::trunc);
				if (summary) summary << "status=failed\nerror=" << error.what() << "\ncombat_latency_claim=false\n";
			} catch (...) {
			}
		} catch (...) {
			try {
				const auto& targetDirectory = outputDir;
				std::filesystem::create_directories(targetDirectory);
				std::ofstream summary(targetDirectory / "combat_self_test_summary.txt", std::ios::out | std::ios::trunc);
				if (summary) summary << "status=failed\nerror=unknown\ncombat_latency_claim=false\n";
			} catch (...) {
			}
		}
	}
}

#endif
