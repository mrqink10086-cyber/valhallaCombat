#pragma once

#ifndef VC_COMBAT_TRACE
#define VC_COMBAT_TRACE 0
#endif

#ifndef VC_COMBAT_TRACE_DIR
#define VC_COMBAT_TRACE_DIR ""
#endif

#include <cstdint>
#include <filesystem>

namespace CombatTrace
{
	enum class EventKind : std::uint8_t
	{
		CostHook,
		PreHit,
		Hit,
		Stop,
		CombatUpdate,
		HitProcessing,
		EngineMeleeHit,
		ProjectileCollision,
		ProjectileApply,
		PrecisionPreHit,
		TimedBlock,
		Tackle,
		StunUpdate,
		StunDamage,
		Execution,
		StaminaRestore
	};

	inline constexpr bool enabled = VC_COMBAT_TRACE != 0;
	inline constexpr std::uint32_t capacity = 65536;

#if VC_COMBAT_TRACE
	class ScopedTimer
	{
	public:
		ScopedTimer(EventKind kind, std::uint64_t actorHandle, std::uint64_t session,
			bool hasStamina = false, double staminaBefore = 0.0, double staminaAfter = 0.0) noexcept;
		~ScopedTimer() noexcept;
		ScopedTimer(const ScopedTimer&) = delete;
		ScopedTimer& operator=(const ScopedTimer&) = delete;
		ScopedTimer(ScopedTimer&&) = delete;
		ScopedTimer& operator=(ScopedTimer&&) = delete;

	private:
		EventKind kind_;
		std::uint64_t actorHandle_;
		std::uint64_t session_;
		std::uint64_t startQpc_;
		std::uint64_t spanId_;
		bool hasStamina_;
		double staminaBefore_;
		double staminaAfter_;
	};

	void record(EventKind kind, std::uint64_t actorHandle, std::uint64_t session,
		bool hasStamina = false, double staminaBefore = 0.0, double staminaAfter = 0.0) noexcept;
	bool flush(const std::filesystem::path& outputDirectory = std::filesystem::path{ VC_COMBAT_TRACE_DIR }) noexcept;
	std::uint32_t bufferedCount() noexcept;
	std::uint64_t droppedCount() noexcept;
#else
	class ScopedTimer
	{
	public:
		constexpr ScopedTimer(EventKind, std::uint64_t, std::uint64_t, bool = false, double = 0.0, double = 0.0) noexcept {}
	};

	inline void record(EventKind, std::uint64_t, std::uint64_t, bool = false, double = 0.0, double = 0.0) noexcept {}
	inline bool flush(const std::filesystem::path& = {}) noexcept { return false; }
	inline constexpr std::uint32_t bufferedCount() noexcept { return 0; }
	inline constexpr std::uint64_t droppedCount() noexcept { return 0; }
#endif
}

#if VC_COMBAT_TRACE
#define VC_TRACE_EVENT(...) ::CombatTrace::record(__VA_ARGS__)
#define VC_TRACE_SCOPE_JOIN_INNER(left, right) left##right
#define VC_TRACE_SCOPE_JOIN(left, right) VC_TRACE_SCOPE_JOIN_INNER(left, right)
#define VC_TRACE_SCOPE(kind, actorHandle, session) \
	::CombatTrace::ScopedTimer VC_TRACE_SCOPE_JOIN(vcCombatTraceScope_, __COUNTER__){ kind, actorHandle, session }
#else
#define VC_TRACE_EVENT(...) ((void)0)
#define VC_TRACE_SCOPE(...) ((void)0)
#endif
