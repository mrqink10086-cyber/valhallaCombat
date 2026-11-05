#include "include/events.h"
#include "include/attackHandler.h"
#include "include/blockHandler.h"
#include "include/executionHandler.h"
#include "include/settings.h"
#include "include/CombatRuntime.h"
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
		if (settings::bAttackStaminaToggle) {
			attackHandler::GetSingleton()->cacheAttack(actor);
		} else if (settings::bStaminaDebuffToggle) {
			staminaHandler::checkStamina(actor);
		}
		break;
	case "attackStop"_h:
		if (settings::bAttackStaminaToggle) {
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
void animEventHandler::ProcessEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>*,RE::BSAnimationGraphEvent* event,RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
{
	if (!event || !event->holder) return;
	auto actor=const_cast<RE::TESObjectREFR*>(event->holder)->As<RE::Actor>(); if (!actor) return;
	const std::string_view name=event->tag.data(); const auto tag=hash(name.data(),name.size());
	if (CombatRuntime::get().ready()) processActor(actor,tag);
	else CombatRuntime::get().schedule(0,[handle=actor->GetHandle(),tag] { auto resolved=handle.get(); if (resolved) processActor(resolved.get(),tag); });
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
