#include "perf_common.h"
#include "perf_rotation.h"

#include <algorithm>
#include <cmath>
#include <immintrin.h>
#include <stdexcept>

namespace {
void runAvx2(const RotationBatch& batch)
{
	std::size_t i = 0;
	for (; i + 8 <= batch.sacb.size(); i += 8) {
		alignas(32) float x[8], y[8], z[8];
		for (std::size_t lane = 0; lane < 8; ++lane) {
			x[lane] = batch.sacb[i + lane];
			y[lane] = batch.cacb[i + lane];
			z[lane] = batch.sb[i + lane];
		}
		const auto vx = _mm256_load_ps(x);
		const auto vy = _mm256_load_ps(y);
		const auto one = _mm256_set1_ps(1.0f);
		const auto vz = _mm256_min_ps(one, _mm256_max_ps(_mm256_set1_ps(-1.0f), _mm256_load_ps(z)));
		const auto horizontal = _mm256_sqrt_ps(_mm256_add_ps(_mm256_mul_ps(vx, vx), _mm256_mul_ps(vy, vy)));
		const auto cb = _mm256_sqrt_ps(_mm256_max_ps(_mm256_setzero_ps(), _mm256_sub_ps(one, _mm256_mul_ps(vz, vz))));
		const auto nonzero = _mm256_cmp_ps(horizontal, _mm256_setzero_ps(), _CMP_GT_OQ);
		const auto ca = _mm256_blendv_ps(one, _mm256_div_ps(vy, horizontal), nonzero);
		const auto sa = _mm256_blendv_ps(_mm256_setzero_ps(), _mm256_div_ps(vx, horizontal), nonzero);
		const auto sacb = _mm256_mul_ps(sa, cb);
		const auto cacb = _mm256_mul_ps(ca, cb);
		alignas(32) float lanes[9][8];
		_mm256_store_ps(lanes[0], ca);
		_mm256_store_ps(lanes[1], _mm256_sub_ps(_mm256_setzero_ps(), sacb));
		_mm256_store_ps(lanes[2], _mm256_mul_ps(sa, vz));
		_mm256_store_ps(lanes[3], sa);
		_mm256_store_ps(lanes[4], cacb);
		_mm256_store_ps(lanes[5], _mm256_sub_ps(_mm256_setzero_ps(), _mm256_mul_ps(ca, vz)));
		_mm256_store_ps(lanes[6], _mm256_setzero_ps());
		_mm256_store_ps(lanes[7], vz);
		_mm256_store_ps(lanes[8], cb);
		for (std::size_t lane = 0; lane < 8; ++lane) {
			auto& m = batch.output[i + lane].entry;
			for (std::size_t entry = 0; entry < 9; ++entry) m[entry / 3][entry % 3] = lanes[entry][lane];
		}
	}
	for (; i < batch.sacb.size(); ++i) {
		productionRotation(batch.output[i], batch.sacb[i], batch.cacb[i], batch.sb[i]);
	}
}
}

void benchmarkAvx2(const RotationBatch& batch, std::size_t samples, std::size_t repetitions)
{
	RotationBatch expected;
	expected.sacb = batch.sacb;
	expected.cacb = batch.cacb;
	expected.sb = batch.sb;
	expected.output.resize(batch.output.size());
	for (std::size_t i = 0; i < expected.sacb.size(); ++i) {
		productionRotation(expected.output[i], expected.sacb[i], expected.cacb[i], expected.sb[i]);
	}
	RotationBatch candidate;
	candidate.sacb = batch.sacb;
	candidate.cacb = batch.cacb;
	candidate.sb = batch.sb;
	candidate.output.resize(batch.output.size());
	runAvx2(candidate);
	for (std::size_t i = 0; i < expected.output.size(); ++i) {
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t column = 0; column < 3; ++column) {
				if (std::abs(expected.output[i].entry[row][column] - candidate.output[i].entry[row][column]) > 0.00002f) {
					throw std::runtime_error("AVX2 rotation candidate does not match production core");
				}
			}
		}
	}
	const auto operations = repetitions * batch.sacb.size();
	perf::print(perf::measure("rotation_avx2", batch.sacb.size(), operations, samples, [&] {
		for (std::size_t i = 0; i < repetitions; ++i) runAvx2(batch);
	}));
}
