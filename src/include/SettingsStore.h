#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace config {
enum class Key {
#define VC_SETTING(type,name,initial,section,page,label,low,high) name,
#include "Settings.def"
#undef VC_SETTING
 count
};
enum class Kind { boolean, real, integer };
struct Definition { const char* name; const char* section; const char* page; const char* label; Kind kind; double initial; double minimum; double maximum; };
constexpr std::size_t count = static_cast<std::size_t>(Key::count);
using Values = std::array<double, count>;
struct Snapshot { Values values{}; std::uint64_t saved{0}; std::uint64_t effective{0}; bool pendingCleanup{false}; std::string message; };
const std::array<Definition, count>& definitions();
Values defaults();
bool validate(const Values& values, std::string& error);
double value(const Values& values, Key key);
class Store {
public:
 explicit Store(std::filesystem::path data = "Data");
 Snapshot snapshot() const;
 bool load(Values& values, std::string& error);
 bool save(const Values& values, std::uint64_t expected, std::string& error);
 void effective(bool pending);
 void cleanupFinished();
 void failure(std::string message);
 const std::filesystem::path& path() const { return canonical; }
private:
 std::filesystem::path root;
 std::filesystem::path canonical;
 mutable std::mutex mutex;
 Snapshot state;
 bool futureSchema{false};
 bool write(const Values& values, std::string& error);
};
Store& store();
}
