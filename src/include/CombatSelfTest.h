#pragma once

#include <filesystem>

#ifndef VC_COMBAT_TRACE
#define VC_COMBAT_TRACE 0
#endif

namespace CombatSelfTest
{
#if VC_COMBAT_TRACE
	void runMainMenu(const std::filesystem::path& outputDir) noexcept;
	void startManualTrigger(const std::filesystem::path& outputDir) noexcept;
#else
	inline void runMainMenu(const std::filesystem::path&) noexcept {}
	inline void startManualTrigger(const std::filesystem::path&) noexcept {}
#endif
}
