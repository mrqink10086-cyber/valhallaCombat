#pragma once
#include "lib/robin_hood.h"
#include <unordered_set>
#include <mutex>
class executionHandler {
public:

	bool executionKeyDown;
	robin_hood::unordered_map<RE::Actor*, RE::Actor*> executionMap;
	static executionHandler* GetSingleton()
	{
		static executionHandler singleton;
		return  std::addressof(singleton);
	}

	void tryPcExecution();
		bool tryAutomatic(RE::Actor*,RE::Actor*);
		float openingDamageMultiplier(RE::Actor*);
		void updatePrompt(float delta);
		std::string targetStatus() const { std::lock_guard lock(statusMutex); return targetInfo; }
		std::string status() const { std::lock_guard lock(statusMutex); return lastStatus; }
		void report(std::string value) { std::lock_guard lock(statusMutex); lastStatus=std::move(value); }
			void reset() { pending.clear(); promptTarget={}; promptCountdown=0; std::lock_guard lock(statusMutex); targetInfo="No target"; }

	void queueExecutionIdle(RE::Actor* a_executor, RE::Actor* a_victim, RE::TESIdleForm* a_executionIdle, int i);

	void queueExecutionIdle(RE::Actor* a_executor, RE::Actor* a_victim, std::vector<RE::TESIdleForm*> a_executionIdleV, int i);

	bool attemptExecute(RE::Actor* executor, RE::Actor* victim);


private:
		struct Eligibility { bool allowed; const char* reason; };
		Eligibility evaluate(RE::Actor*,RE::Actor*,bool checkPending=true);
		const std::vector<RE::TESIdleForm*>* idlesFor(RE::Actor*,RE::Actor*);
		RE::Actor* selectTarget(RE::Actor*);
		std::string targetInfo{"No target"};
		RE::ActorHandle promptTarget;
		float promptCountdown{0};
		mutable std::mutex statusMutex;
		std::string lastStatus{"No execution requested"};

		std::unordered_set<RE::ActorHandle> pending;
		std::uint64_t queued{0};

};

namespace inlineUtils
{
	inline void sendConsoleCommand(std::string a_command)
	{
		const auto scriptFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::Script>();
		const auto script = scriptFactory ? scriptFactory->Create() : nullptr;
		if (script) {
			const auto selectedRef = RE::Console::GetSelectedRef();
			script->SetCommand(a_command);
			script->CompileAndRun(selectedRef.get());
			delete script;
		}
	}

	inline std::string getRandomStr(std::vector<std::string> in) {
		return in[rand() % in.size()];
	}

	inline RE::TESIdleForm* getRandomIdle(std::vector<RE::TESIdleForm*> in) {
		auto i = rand() % in.size();
		(i);
		return in[i];
	}



}

