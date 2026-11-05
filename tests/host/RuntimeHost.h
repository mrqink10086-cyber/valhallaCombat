#pragma once
#include <functional>
#include <mutex>
#include <vector>
#include <exception>
namespace logger { template<class... Args> void error(const char*,Args&&...) {} }
namespace SKSE {
struct TaskInterface {
 std::mutex mutex; std::vector<std::function<void()>> tasks;
 void AddTask(std::function<void()> task) { std::lock_guard lock(mutex); tasks.push_back(std::move(task)); }
 void drain() { std::vector<std::function<void()>> copy; { std::lock_guard lock(mutex); copy.swap(tasks); } for (auto& task:copy) task(); }
};
inline TaskInterface* GetTaskInterface() { static TaskInterface tasks; return &tasks; }
}
