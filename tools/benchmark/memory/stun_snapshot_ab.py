#!/usr/bin/env python3
"""Build a host A/B microbenchmark from current and Git-HEAD stun production code."""

from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / "build/perf-memory"
BENCH = '''#define main stun_regression_main
#include "../../../tests/stun_regression.cpp"
#undef main

#include <chrono>

int main()
{
    constexpr int batchCount = 7;
    constexpr int operationsPerBatch = 5000;
    auto handler = stunHandler::GetSingleton();
    RE::Actor attacker;
    std::vector<RE::Actor> population(32);
    for (auto& actor : population)
        handler->processStunDamage(stunHandler::powerAttack, nullptr, &attacker, &actor, 1);

    std::puts("implementation,scenario,batch,operations,total_ns,ns_per_op,allocations");
    for (int batch = 0; batch < batchCount; ++batch) {
        allocationCount.store(0, std::memory_order_relaxed);
        const auto start = std::chrono::steady_clock::now();
        trackAllocations.store(true, std::memory_order_relaxed);
        for (int i = 0; i < operationsPerBatch; ++i)
            handler->modStun(&population.front(), (i & 1) ? 0.25f : -0.25f);
        trackAllocations.store(false, std::memory_order_relaxed);
        const auto end = std::chrono::steady_clock::now();
        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        const auto allocations = allocationCount.load(std::memory_order_relaxed);
        std::printf("%s,modStun,%d,%d,%lld,%.2f,%zu\\n",
#ifdef STUN_FULL_REBUILD_BASELINE
            "full_rebuild",
#else
            "delta",
#endif
            batch + 1, operationsPerBatch, static_cast<long long>(totalNs),
            static_cast<double>(totalNs) / operationsPerBatch, allocations);
    }

    std::vector<RE::Actor> framePopulation(32);
    for (int batch = 0; batch < batchCount; ++batch) {
        handler->reset();
        for (auto& actor : framePopulation)
            handler->processStunDamage(stunHandler::powerAttack, nullptr, &attacker, &actor, 1);
        framePopulation.front().inCombat = false;
        handler->modStun(&framePopulation.front(), -99.0f);
        handler->update(3.0f);
        handler->update(0.001f);  // warm reusable delta-vector capacity

        allocationCount.store(0, std::memory_order_relaxed);
        const auto start = std::chrono::steady_clock::now();
        trackAllocations.store(true, std::memory_order_relaxed);
        for (int i = 0; i < operationsPerBatch; ++i)
            handler->update(0.001f);
        trackAllocations.store(false, std::memory_order_relaxed);
        const auto end = std::chrono::steady_clock::now();
        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        const auto allocations = allocationCount.load(std::memory_order_relaxed);
        std::printf("%s,update_one_change,%d,%d,%lld,%.2f,%zu\\n",
#ifdef STUN_FULL_REBUILD_BASELINE
            "full_rebuild",
#else
            "delta",
#endif
            batch + 1, operationsPerBatch, static_cast<long long>(totalNs),
            static_cast<double>(totalNs) / operationsPerBatch, allocations);
    }
}
'''


def git_show(path: str) -> str:
    return subprocess.run(
        ["git", "show", f"HEAD:{path}"], cwd=ROOT, check=True,
        capture_output=True, text=True,
    ).stdout


def write_variant(name: str, source: str, header: str) -> Path:
    directory = OUT / name
    (directory / "include").mkdir(parents=True, exist_ok=True)
    (directory / "include/stunHandler.h").write_text(header, encoding="utf-8")
    functions = re.sub(r"^#include[^\n]*\n", "", source, flags=re.M)
    (directory / "stun_functions.inc").write_text(functions, encoding="utf-8")
    (directory / "stun_snapshot_bench.cpp").write_text(BENCH, encoding="utf-8")
    return directory


def run_variant(name: str, directory: Path, baseline: bool) -> None:
    executable = OUT / f"stun_snapshot_bench_{name}.exe"
    csv_path = OUT / f"{name}.csv"
    command = [
        "clang++", "-std=c++20", "-O2", "-DNDEBUG", "-Wall", "-Wextra",
        "-Werror", "-Wno-return-type", "-Wno-microsoft-include",
    ]
    if baseline:
        command.append("-DSTUN_FULL_REBUILD_BASELINE")
    command.extend([
        f"-I{directory}", f"-I{ROOT / 'src'}",
        str(directory / "stun_snapshot_bench.cpp"), "-o", str(executable),
    ])
    subprocess.run(command, cwd=ROOT, check=True)
    result = subprocess.run([str(executable)], cwd=ROOT, check=True, capture_output=True, text=True)
    csv_path.write_text(result.stdout, encoding="utf-8")


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    baseline = write_variant(
        "baseline", git_show("src/bin/stunHandler.cpp"), git_show("src/include/stunHandler.h")
    )
    current = write_variant(
        "delta", (ROOT / "src/bin/stunHandler.cpp").read_text(encoding="utf-8-sig"),
        (ROOT / "src/include/stunHandler.h").read_text(encoding="utf-8-sig"),
    )
    run_variant("baseline", baseline, True)
    run_variant("delta", current, False)
    print(OUT / "baseline.csv")
    print(OUT / "delta.csv")


if __name__ == "__main__":
    main()
