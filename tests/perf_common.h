#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace perf {
using Clock = std::chrono::steady_clock;

struct Result {
	std::string_view name;
	std::size_t cardinality;
	std::size_t operations;
	std::size_t samples;
	double p50;
	double p95;
	double p99;
};

inline double percentile(const std::vector<double>& sorted, double p)
{
	const auto index = static_cast<std::size_t>(p * static_cast<double>(sorted.size() - 1));
	return sorted[index];
}

template <class Work>
Result measure(std::string_view name, std::size_t cardinality, std::size_t operations,
	std::size_t samples, Work&& work)
{
	for (int i = 0; i < 3; ++i) work();
	std::vector<double> values;
	values.reserve(samples);
	for (std::size_t i = 0; i < samples; ++i) {
		const auto start = Clock::now();
		work();
		const auto elapsed = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
		values.push_back(elapsed / static_cast<double>(operations));
	}
	std::sort(values.begin(), values.end());
	return { name, cardinality, operations, samples,
		percentile(values, 0.50), percentile(values, 0.95), percentile(values, 0.99) };
}

template <class Setup, class Work, class Cleanup>
Result measurePrepared(std::string_view name, std::size_t cardinality, std::size_t operations,
	std::size_t samples, Setup&& setup, Work&& work, Cleanup&& cleanup)
{
	for (int i = 0; i < 3; ++i) {
		setup();
		work();
		cleanup();
	}
	std::vector<double> values;
	values.reserve(samples);
	for (std::size_t i = 0; i < samples; ++i) {
		setup();
		const auto start = Clock::now();
		work();
		const auto elapsed = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
		cleanup();
		values.push_back(elapsed / static_cast<double>(operations));
	}
	std::sort(values.begin(), values.end());
	return { name, cardinality, operations, samples,
		percentile(values, 0.50), percentile(values, 0.95), percentile(values, 0.99) };
}

inline void print(const Result& result)
{
	std::cout << result.name << ',' << result.cardinality << ',' << result.operations << ','
		<< result.samples << ',' << result.p50 << ',' << result.p95 << ',' << result.p99 << '\n';
}

inline std::size_t sampleCount(int argc, char** argv)
{
	if (argc < 2) return 41;
	try {
		return std::max<std::size_t>(5, static_cast<std::size_t>(std::stoull(argv[1])));
	} catch (...) {
		return 41;
	}
}

inline void printHeader()
{
	std::cout << "benchmark,cardinality,operations_per_sample,samples,p50_ns_per_op,p95_ns_per_op,p99_ns_per_op\n";
}
}
