#include "SimpleIni.h"
#include "include/Hooks.h"
#include "include/data.h"
#include "include/debuffHandler.h"
#include "include/events.h"
#include "include/lib/TrueHUDAPI.h"
#include "ValhallaCombat.hpp"
#include "include/settings.h"
#include "include/Utils.h"
#include "include/lib/ValhallaCombatAPI.h"
#include "include/ModAPI.h"
#include "include/attackHandler.h"
#include "include/executionHandler.h"
#include "include/Menu.h"
#include "include/OwnedEffects.h"
#include "include/CombatTrace.h"
#include "include/CombatSelfTest.h"
#include "include/EnemyClassifier.h"
void endCombatSession();
namespace {
bool hooksInstalled = false;
void dispatchLifecycle(CombatRuntime::Task task) {
 if (auto tasks=SKSE::GetTaskInterface()) {
  tasks->AddTask(std::move(task));
 } else {
  logger::error("Cannot dispatch combat lifecycle without the SKSE task interface");
 }
}
class MenuLifecycle final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
 RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override {
  const bool main=event && event->opening && event->menuName==RE::MainMenu::MENU_NAME;
  const auto action=[main] { CombatRuntime::get().resetClock(); if (main) endCombatSession(); };
  dispatchLifecycle(action);
  return RE::BSEventNotifyControl::kContinue;
 }
};
MenuLifecycle menuLifecycle;
}
void initTrueHUDAPI() {
	auto val = ValhallaCombat::GetSingleton();
	val->ersh_TrueHUD = reinterpret_cast<TRUEHUD_API::IVTrueHUD3*>(TRUEHUD_API::RequestPluginAPI(TRUEHUD_API::InterfaceVersion::V3));
	if (val->ersh_TrueHUD) {
		logger::info("Obtained TruehudAPI - {0:x}", (uintptr_t)val->ersh_TrueHUD);
		settings::facts::TrueHudAPI_Obtained = true;
	} else {
		logger::info("TrueHUD API not found.");
		settings::facts::TrueHudAPI_Obtained = false;
	}
}

void initPrecisionAPI() {
	auto val = ValhallaCombat::GetSingleton();
	val->ersh_Precision = reinterpret_cast<PRECISION_API::IVPrecision1*>(PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V1));
	if (val->ersh_Precision) {
		logger::info("Obtained PrecisionAPI - {0:x}", (uintptr_t)val->ersh_Precision);
		settings::facts::PrecisionAPI_Obtained = true;
		auto res = val->ersh_Precision->AddPreHitCallback(SKSE::GetPluginHandle(), blockHandler::precisionPrehitCallbackFunc);
		if (res == PRECISION_API::APIResult::OK || res == PRECISION_API::APIResult::AlreadyRegistered) {
			logger::info("Collision prehit callback successfully registered.");
		}
	} else {
		logger::info("Precision API not found.");
		settings::facts::PrecisionAPI_Obtained = false;
	}
}

void onDataLoaded() 
{
	CombatRuntime::get().initialize();
	logger::info("Combat owner initialized on thread {}", GetCurrentThreadId());
	settings::readSettings();
	events::registerAllEventHandlers();
	if (auto ui=RE::UI::GetSingleton()) ui->AddEventSink<RE::MenuOpenCloseEvent>(&menuLifecycle);
	data::loadData();
	encounters::load();
	blockHandler::GetSingleton()->init();
	blockHandler::EldenCounterCompatibility::attemptInit(); //todo: fix this jank
	hooksInstalled = Hooks::install();
#if VC_COMBAT_TRACE
	CombatSelfTest::startManualTrigger(VC_COMBAT_TRACE_DIR);
#endif
}

void onPostLoad() {
	logger::info("Initializing API fetch...");
	initTrueHUDAPI();
	initPrecisionAPI();
	logger::info("...done");
}

void onPostLoadGame() {
	if (!hooksInstalled) {
		logger::error("Combat session disabled because hook validation failed");
		return;
	}
	CombatRuntime::get().beginSession();
	OwnedEffects::update();
	settings::reapplySettings();
}

void endCombatSession()
{
	CombatRuntime::get().endSession();
	animEventHandler::reset();
	debuffHandler::GetSingleton()->reset();
	stunHandler::GetSingleton()->reset();
	blockHandler::EldenCounterCompatibility::reset();
	blockHandler::GetSingleton()->reset();
	attackHandler::GetSingleton()->reset();
	executionHandler::GetSingleton()->reset();
	OwnedEffects::restore();
#if VC_COMBAT_TRACE
	if (auto tasks=SKSE::GetTaskInterface()) tasks->AddTask([] {
		CombatTrace::flush(VC_COMBAT_TRACE_DIR);
	});
#endif
}



void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
	switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostPostLoad:
			Menu::registerMenu();
			break;
	case SKSE::MessagingInterface::kDataLoaded:
			dispatchLifecycle(onDataLoaded);
		break;
	case SKSE::MessagingInterface::kPostLoad:
		onPostLoad();
		break;
	case SKSE::MessagingInterface::kPostLoadGame:
			dispatchLifecycle(a_msg->data != nullptr ? onPostLoadGame : endCombatSession);
		break;
		case SKSE::MessagingInterface::kPreLoadGame:
				dispatchLifecycle(endCombatSession);
			break;
		case SKSE::MessagingInterface::kNewGame:
				dispatchLifecycle([] { endCombatSession(); onPostLoadGame(); });
			break;
	}
}

void onSKSEInit()
{
}

namespace
{
	void InitializeLog()
	{
#ifndef NDEBUG
		auto sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
#else
		auto path = logger::log_directory();
		if (!path) {
			util::report_and_fail("Failed to find standard logging directory"sv);
		}

		*path /= fmt::format("{}.log"sv, Plugin::NAME);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
#endif

#ifndef NDEBUG
		const auto level = spdlog::level::trace;
#else
		const auto level = spdlog::level::info;
#endif

		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
		log->set_level(level);
		log->flush_on(level);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("%s(%#): [%^%l%$] %v"s);
	}
}

std::string wstring2string(const std::wstring& wstr, UINT CodePage)

{

	std::string ret;

	int len = WideCharToMultiByte(CodePage, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);

	ret.resize((size_t)len, 0);

	WideCharToMultiByte(CodePage, 0, wstr.c_str(), (int)wstr.size(), &ret[0], len, NULL, NULL);

	return ret;

}


extern "C" DLLEXPORT bool  SKSEPlugin_Query(const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info)
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Plugin::NAME.data();
	a_info->version = Plugin::VERSION[0];

	if (a_skse->IsEditor()) {
		logger::critical("Loaded in editor, marking as incompatible"sv);
		return false;
	}

	const auto ver = a_skse->RuntimeVersion();
	if (ver < SKSE::RUNTIME_SSE_1_5_39) {
		logger::critical(FMT_STRING("Unsupported runtime version {}"), ver.string());
		return false;
	}

	return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() {
	SKSE::PluginVersionData v;

	v.PluginVersion(Plugin::VERSION);
	v.PluginName(Plugin::NAME);
	v.AuthorName("D7ry");

	v.UsesAddressLibrary();
	v.CompatibleVersions({ SKSE::RUNTIME_SSE_LATEST });
	v.UsesNoStructs();

	return v;
}();


extern "C" DLLEXPORT bool  SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
    #ifndef NDEBUG
	while (!IsDebuggerPresent()) { Sleep(100); }
#endif
	REL::Module::reset();
	InitializeLog();
	logger::info("{} v{}"sv, Plugin::NAME, Plugin::VERSION.string());

	SKSE::Init(a_skse);
	OwnedEffects::initialize();

	auto messaging = SKSE::GetMessagingInterface();
	if (!messaging->RegisterListener("SKSE", MessageHandler)) {
		return false;
	}
	
	onSKSEInit();


	return true;
}


extern "C" DLLEXPORT void*  RequestPluginAPI(const VAL_API::InterfaceVersion a_interfaceVersion)
{
	//auto api = Messaging::TrueHUDInterface::GetSingleton();
	auto api = ModAPI::VALInterface::GetSingleton();
	logger::info("ValhallaCombat::RequestPluginAPI called");

	switch (a_interfaceVersion) {
	case VAL_API::InterfaceVersion::V1:
	case VAL_API::InterfaceVersion::V2:
		logger::info("ValhallaCombat::RequestPluginAPI returned the API singleton");
		return static_cast<void*>(api);
	}
	logger::info("ValhallaCombat::RequestPluginAPI requested the wrong interface version");
	return nullptr;
}
