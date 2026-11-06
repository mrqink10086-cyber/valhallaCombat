#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace logger {
template <class... Args> void info(const char*, Args&&...) {}
template <class... Args> void error(const char*, Args&&...) {}
}
namespace SKSE {
struct TaskInterface {
 std::mutex mutex;
 std::condition_variable changed;
 std::vector<std::function<void()>> pending;
 void AddTask(std::function<void()> task) {
  { std::scoped_lock lock(mutex); pending.push_back(std::move(task)); }
  changed.notify_one();
 }
 bool wait(std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex);
  return changed.wait_for(lock, timeout, [&] { return !pending.empty(); });
 }
 void drain() {
  std::vector<std::function<void()>> batch;
  { std::scoped_lock lock(mutex); batch.swap(pending); }
  for (auto& task : batch) task();
 }
};
TaskInterface* GetTaskInterface() { static TaskInterface tasks; return &tasks; }
}
const auto owner = std::this_thread::get_id();
std::atomic<int> runs=0, flushes=0;
void check(bool passed, const char* message) { if (!passed) throw std::runtime_error(message); }
namespace CombatTrace {
void flush(const std::filesystem::path&) { check(std::this_thread::get_id()==owner,"trace flush ran on file watcher"); ++flushes; }
}
namespace CombatSelfTest {
void runMainMenu(const std::filesystem::path& outputDir) {
 check(std::this_thread::get_id()==owner,"self-test ran on file watcher");
 check(!std::filesystem::exists(outputDir/"combat_self_test.request"),"request not consumed before dispatch");
 ++runs;
}
#include "self_test_trigger_functions.inc"
}
int main() {
 try {
  using namespace std::chrono_literals;
  const auto output = std::filesystem::current_path()/"self_test_trigger_output";
  std::filesystem::create_directories(output);
  const auto request = output/"combat_self_test.request";
  std::filesystem::remove(request);
  auto* tasks=SKSE::GetTaskInterface();
  CombatSelfTest::startManualTrigger(output);
  CombatSelfTest::startManualTrigger(output);
  check(!tasks->wait(350ms) && runs==0,"manual self-test started without request");
  { std::ofstream file(request); file << "run\n"; check(bool(file),"cannot create request"); }
  check(tasks->wait(2s),"request did not queue a self-test");
  check(runs==0 && flushes==0,"file watcher executed engine work directly");
  tasks->drain();
  check(runs==1 && flushes==1,"manual request must dispatch exactly one test and flush");
  check(!tasks->wait(350ms),"one request dispatched repeatedly");
  std::puts("Manual request waits for user and executes only on task owner: PASS");
 } catch (const std::exception& error) {
  std::fprintf(stderr,"FAIL: %s\n",error.what());
  return 1;
 }
}
