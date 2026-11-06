#include "include/events.h"
#include "include/attackHandler.h"
#include "include/blockHandler.h"
#include "include/executionHandler.h"
#include "include/settings.h"
#include "include/CombatRuntime.h"
#include "include/CombatTrace.h"
#include <mutex>
#include <vector>
//all credits to Bingle&Ersh
constexpr uint32_t hash(const char* data, size_t const size) noexcept
{
	uint32_t hash = 5381;

	for (const char* c = data; c < data + size; ++c) {
		hash = ((hash << 5) + hash) + (unsigned char)*c;
	}

	return hash;
}

constexpr uint32_t operator"" _h(const char* str, size_t size) noexcept
{
	return hash(str, size);
}

static void processActor(RE::Actor* actor,std::uint32_t tag)
{
	if (!actor || !CombatRuntime::get().ready()) return;
	switch (tag) {
	case "preHitFrame"_h:
#if VC_COMBAT_TRACE
		VC_TRACE_EVENT(CombatTrace::EventKind::PreHit, actor->GetHandle().native_handle(), CombatRuntime::get().session());
#endif
		if (attackHandler::enabledFor(actor)) {
			attackHandler::GetSingleton()->cacheAttack(actor);
		} else if (settings::bStaminaDebuffToggle) {
			staminaHandler::checkStamina(actor);
		}
		break;
	case "attackStop"_h:
#if VC_COMBAT_TRACE
		VC_TRACE_EVENT(CombatTrace::EventKind::Stop, actor->GetHandle().native_handle(), CombatRuntime::get().session());
#endif
		if (attackHandler::enabledFor(actor)) {
			attackHandler::GetSingleton()->checkout(actor);
		}
		if (settings::bStaminaDebuffToggle) {
			staminaHandler::checkStamina(actor);
		}
		break;
	//case "blockStop"_h:
	//	if (settings::bTimedBlockToggle && a_event->holder->IsPlayerRef()) {
	//		blockHandler::GetSingleton()->onBlockStop();
	//	}
	//	break;
	case "TKDR_IFrameEnd"_h:
		staminaHandler::checkStamina(actor);
		break;
	case "Dodge"_h:
		staminaHandler::checkStamina(actor);
		break;
	}
}
namespace {
struct PendingAnimation { RE::ActorHandle actor; std::uint32_t tag; std::uint64_t session; };
std::mutex animationMutex;
std::vector<PendingAnimation> pendingAnimations;
std::vector<PendingAnimation> animationBatch;
bool animationFlushing=false;
bool animationFlushQueued=false;
std::uint64_t animationFlushSession=0;
}
void animEventHandler::reset()
{
	std::lock_guard lock(animationMutex);
	pendingAnimations.clear();
	animationFlushQueued=false;
}
void animEventHandler::flush()
{
	if (!CombatRuntime::get().ready()) return;
	if (animationFlushing) return;
	animationFlushing=true;
	struct Scope {
		bool& flag;
		std::vector<PendingAnimation>& batch;
		~Scope() { batch.clear(); flag=false; }
	} scope{animationFlushing,animationBatch};
	{ std::lock_guard lock(animationMutex); animationBatch.swap(pendingAnimations); }
	for (const auto& event : animationBatch) {
		if (event.session!=CombatRuntime::get().session() || !CombatRuntime::get().ready()) continue;
		auto actor=event.actor.get();
		if (actor) processActor(actor.get(),event.tag);
	}
}
void animEventHandler::submit(RE::Actor* actor, std::uint32_t tag)
{
	if (!actor) return;
	switch (tag) {
	case "preHitFrame"_h: case "attackStop"_h: case "TKDR_IFrameEnd"_h: case "Dodge"_h: break;
	default: return;
	}
	if (CombatRuntime::get().ready() && !animationFlushing) {
		flush();
		animationFlushing=true;
		struct Scope { bool& flag; ~Scope() { flag=false; } } scope{animationFlushing};
		processActor(actor,tag);
		return;
	}
	{
		std::lock_guard lock(animationMutex);
		const auto session=CombatRuntime::get().session();
		pendingAnimations.push_back({actor->GetHandle(),tag,session});
		if (!animationFlushQueued || animationFlushSession!=session) {
			animationFlushQueued=true;
			animationFlushSession=session;
			if (!CombatRuntime::get().schedule(0,[session] {
				{ std::lock_guard lock(animationMutex); if (animationFlushSession==session) animationFlushQueued=false; }
				flush();
			})) {
				animationFlushQueued=false;
				pendingAnimations.pop_back();
			}
		}
	}
}
void animEventHandler::ProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>*,RE::BSAnimationGraphEvent* event,RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
{
	if (!event || !event->holder) return;
	auto actor=const_cast<RE::TESObjectREFR*>(event->holder)->As<RE::Actor>(); if (!actor) return;
	const std::string_view name=event->tag.data(); const auto tag=hash(name.data(),name.size());
	submit(actor,tag);
}

EventResult animEventHandler::ProcessEvent_NPC(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_sink, RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource)
{
	ProcessEvent(a_sink, a_event, a_eventSource);
	return _ProcessEvent_NPC(a_sink, a_event, a_eventSource);
}

EventResult animEventHandler::ProcessEvent_PC(RE::BSTEventSink<RE::BSAnimationGraphEvent>* a_sink, RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource)
{
	ProcessEvent(a_sink, a_event, a_eventSource);
	return _ProcessEvent_PC(a_sink, a_event, a_eventSource);
}
