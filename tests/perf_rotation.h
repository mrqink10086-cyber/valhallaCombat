#pragma once

#include <cstddef>
#include <vector>

namespace RE {
struct NiMatrix3 { float entry[3][3]{}; };
}

struct RotationBatch {
	std::vector<float> sacb;
	std::vector<float> cacb;
	std::vector<float> sb;
	mutable std::vector<RE::NiMatrix3> output;
};

void productionRotation(RE::NiMatrix3& output, float sacb, float cacb, float sb);
void benchmarkAvx2(const RotationBatch& batch, std::size_t samples, std::size_t repetitions);
