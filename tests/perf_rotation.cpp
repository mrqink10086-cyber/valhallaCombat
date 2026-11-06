#include "perf_common.h"
#include "perf_rotation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace RE {
enum class ActorValue { kStamina };
struct ActorValueOwner {
	float GetActorValue(ActorValue) const { return 0.0f; }
	void RestoreActorValue(ActorValue, float) {}
};
struct Actor {
	ActorValueOwner* AsActorValueOwner() { return &owner; }
	ActorValueOwner owner;
};
}
namespace Utils::Actor {
void damageav(RE::Actor*, RE::ActorValue, float);
bool tryDamageAv(RE::Actor*, RE::ActorValue, float);
}
namespace inlineUtils {
void SetRotationMatrix(RE::NiMatrix3&, float, float, float);
}

#include "utils_functions.inc"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace {
constexpr std::size_t targetOperations = 256;
constexpr std::size_t workerCount = 4;
volatile float outputSink{};

std::size_t repetitionsFor(std::size_t n)
{
	return std::max<std::size_t>(1, (targetOperations + n - 1) / n);
}

void runScalarNoInline(const RotationBatch& batch)
{
	for (std::size_t i = 0; i < batch.sacb.size(); ++i) {
		productionRotation(batch.output[i], batch.sacb[i], batch.cacb[i], batch.sb[i]);
	}
}

void runContiguousBatch(const RotationBatch& batch)
{
	for (std::size_t i = 0; i < batch.sacb.size(); ++i) {
		inlineUtils::SetRotationMatrix(batch.output[i], batch.sacb[i], batch.cacb[i], batch.sb[i]);
	}
}

void runSse2(const RotationBatch& batch)
{
	std::size_t i = 0;
	for (; i + 4 <= batch.sacb.size(); i += 4) {
		alignas(16) float x[4], y[4], z[4];
		for (std::size_t lane = 0; lane < 4; ++lane) {
			x[lane] = batch.sacb[i + lane];
			y[lane] = batch.cacb[i + lane];
			z[lane] = batch.sb[i + lane];
		}
		const auto vx = _mm_load_ps(x);
		const auto vy = _mm_load_ps(y);
		const auto one = _mm_set1_ps(1.0f);
		const auto vz = _mm_min_ps(one, _mm_max_ps(_mm_set1_ps(-1.0f), _mm_load_ps(z)));
		const auto horizontal = _mm_sqrt_ps(_mm_add_ps(_mm_mul_ps(vx, vx), _mm_mul_ps(vy, vy)));
		const auto cb = _mm_sqrt_ps(_mm_max_ps(_mm_setzero_ps(), _mm_sub_ps(one, _mm_mul_ps(vz, vz))));
		const auto nonzero = _mm_cmpgt_ps(horizontal, _mm_setzero_ps());
		const auto ca = _mm_or_ps(_mm_and_ps(nonzero, _mm_div_ps(vy, horizontal)), _mm_andnot_ps(nonzero, one));
		const auto sa = _mm_and_ps(nonzero, _mm_div_ps(vx, horizontal));
		const auto sacb = _mm_mul_ps(sa, cb);
		const auto cacb = _mm_mul_ps(ca, cb);
		alignas(16) float lanes[9][4];
		_mm_store_ps(lanes[0], ca);
		_mm_store_ps(lanes[1], _mm_sub_ps(_mm_setzero_ps(), sacb));
		_mm_store_ps(lanes[2], _mm_mul_ps(sa, vz));
		_mm_store_ps(lanes[3], sa);
		_mm_store_ps(lanes[4], cacb);
		_mm_store_ps(lanes[5], _mm_sub_ps(_mm_setzero_ps(), _mm_mul_ps(ca, vz)));
		_mm_store_ps(lanes[6], _mm_setzero_ps());
		_mm_store_ps(lanes[7], vz);
		_mm_store_ps(lanes[8], cb);
		for (std::size_t lane = 0; lane < 4; ++lane) {
			auto& m = batch.output[i + lane].entry;
			for (std::size_t entry = 0; entry < 9; ++entry) m[entry / 3][entry % 3] = lanes[entry][lane];
		}
	}
	for (; i < batch.sacb.size(); ++i) {
		inlineUtils::SetRotationMatrix(batch.output[i], batch.sacb[i], batch.cacb[i], batch.sb[i]);
	}
}

class PersistentPool {
public:
	PersistentPool() { for (std::size_t i = 0; i < workerCount; ++i) workers.emplace_back([this, i] { worker(i); }); }
	~PersistentPool()
	{
		{
			std::lock_guard lock(mutex);
			stopping = true;
			++generation;
		}
		start.notify_all();
		for (auto& thread : workers) thread.join();
	}

	void run(const RotationBatch& batch)
	{
		std::unique_lock lock(mutex);
		size = batch.sacb.size();
		input = &batch;
		remaining = workers.size();
		++generation;
		start.notify_all();
		finished.wait(lock, [&] { return remaining == 0; });
	}

private:
	std::mutex mutex;
	std::condition_variable start, finished;
	std::vector<std::thread> workers;
	const RotationBatch* input{};
	std::size_t size{}, remaining{};
	std::uint64_t generation{};
	bool stopping{};

	void worker(std::size_t index)
	{
		std::uint64_t observed = 0;
		std::unique_lock lock(mutex);
		for (;;) {
			start.wait(lock, [&] { return stopping || generation != observed; });
			if (stopping) return;
			observed = generation;
			const auto* batch = input;
			const auto count = size;
			const auto begin = count * index / workers.size();
			const auto end = count * (index + 1) / workers.size();
			lock.unlock();
			for (std::size_t i = begin; i < end; ++i) {
				inlineUtils::SetRotationMatrix(batch->output[i], batch->sacb[i], batch->cacb[i], batch->sb[i]);
			}
			lock.lock();
			if (--remaining == 0) finished.notify_one();
		}
	}
};

bool avx2Available()
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	int regs[4]{};
	__cpuid(regs, 1);
	const bool hasAvx = (regs[2] & (1 << 28)) != 0;
	const bool hasOsxsave = (regs[2] & (1 << 27)) != 0;
	if (!hasAvx || !hasOsxsave || (_xgetbv(0) & 0x6) != 0x6) return false;
	__cpuidex(regs, 7, 0);
	return (regs[1] & (1 << 5)) != 0;
#else
	return false;
#endif
}

std::string cpuBrand()
{
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
	int regs[4]{};
	__cpuid(regs, 0x80000000);
	if (static_cast<unsigned int>(regs[0]) < 0x80000004) return "unknown";
	char brand[49]{};
	auto* words = reinterpret_cast<int*>(brand);
	for (int leaf = 0; leaf < 3; ++leaf) {
		__cpuid(regs, 0x80000002 + leaf);
		for (int i = 0; i < 4; ++i) words[leaf * 4 + i] = regs[i];
	}
	return brand;
#else
	return "unknown";
#endif
}

float checksum(const RotationBatch& batch)
{
	float sum = 0.0f;
	for (const auto& matrix : batch.output) sum += matrix.entry[0][0] + matrix.entry[1][1] + matrix.entry[2][2];
	return sum;
}

void fillBatch(RotationBatch& batch, std::size_t n)
{
	batch.sacb.resize(n);
	batch.cacb.resize(n);
	batch.sb.resize(n);
	batch.output.resize(n);
	for (std::size_t i = 0; i < n; ++i) {
		const auto a = static_cast<float>((i * 17) % 101) / 101.0f;
		const auto b = static_cast<float>((i * 29 + 7) % 97) / 97.0f;
		const auto c = static_cast<float>((i * 43 + 13) % 89) / 89.0f;
		batch.sacb[i] = (a * 2.0f - 1.0f) * 0.8f;
		batch.cacb[i] = (b * 2.0f - 1.0f) * 0.8f;
		batch.sb[i] = c * 2.0f - 1.0f;
	}
}

void requireSame(const RotationBatch& expected, const RotationBatch& actual)
{
	for (std::size_t i = 0; i < expected.output.size(); ++i) {
		for (std::size_t row = 0; row < 3; ++row) {
			for (std::size_t column = 0; column < 3; ++column) {
				if (std::abs(expected.output[i].entry[row][column] - actual.output[i].entry[row][column]) > 0.00002f) {
					throw std::runtime_error("rotation candidate does not match production core");
				}
			}
		}
	}
}
}

__declspec(noinline) void productionRotation(RE::NiMatrix3& output, float sacb, float cacb, float sb)
{
	inlineUtils::SetRotationMatrix(output, sacb, cacb, sb);
}

int main(int argc, char** argv)
{
	const auto samples = perf::sampleCount(argc, argv);
	constexpr std::array<std::size_t, 4> sizes{ 1, 32, 256, 4096 };
	std::cout << "# cpu=" << cpuBrand() << ",avx2=" << (avx2Available() ? "yes" : "no") << ",workers=" << workerCount << '\n';
	perf::printHeader();
	PersistentPool pool;

	for (const auto n : sizes) {
		const auto repetitions = repetitionsFor(n);
		RotationBatch batch;
		fillBatch(batch, n);
		const auto operations = repetitions * n;

		auto runRepeated = [&](auto&& transform) {
			for (std::size_t repeat = 0; repeat < repetitions; ++repeat) transform(batch);
		};
		perf::print(perf::measure("rotation_scalar_noinline", n, operations, samples, [&] { runRepeated(runScalarNoInline); }));
		outputSink = checksum(batch);
		perf::print(perf::measure("rotation_contiguous_batch", n, operations, samples, [&] { runRepeated(runContiguousBatch); }));
		outputSink = checksum(batch);
		perf::print(perf::measure("rotation_sse2", n, operations, samples, [&] { runRepeated(runSse2); }));
		outputSink = checksum(batch);
		perf::print(perf::measure("rotation_worker_pool_4", n, operations, samples, [&] { runRepeated([&](const auto& data) { pool.run(data); }); }));
		outputSink = checksum(batch);

		RotationBatch production, candidate;
		fillBatch(production, n);
		fillBatch(candidate, n);
		runContiguousBatch(production);
		runSse2(candidate);
		requireSame(production, candidate);
		if (avx2Available()) benchmarkAvx2(batch, samples, repetitions);
	}
	return outputSink == 0.0f ? 1 : 0;
}
