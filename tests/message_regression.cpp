#include <cstdint>
#include <cstdio>
#include <stdexcept>
namespace SKSE { struct MessagingInterface { enum { kPostPostLoad,kDataLoaded,kPostLoad,kPostLoadGame,kPreLoadGame,kNewGame }; struct Message { int type; void* data; }; }; }
inline int started=0,ended=0;
namespace Menu { void registerMenu() {} }
void onDataLoaded() {} void onPostLoad() {} void onPostLoadGame() { ++started; } void endCombatSession() { ++ended; }
#include "message_functions.inc"
int main() {
 SKSE::MessagingInterface::Message success{SKSE::MessagingInterface::kPostLoadGame,reinterpret_cast<void*>(std::uintptr_t{1})};
 MessageHandler(&success);
 SKSE::MessagingInterface::Message failure{SKSE::MessagingInterface::kPostLoadGame,nullptr}; MessageHandler(&failure);
 if (started!=1 || ended!=1) throw std::runtime_error("load result encoding regression");
 std::puts("SKSE pointer-encoded successful/failed load result: PASS");
}
