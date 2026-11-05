#pragma once

#include "SKSE/Trampoline.h"
#include "include/blockHandler.h"
#include "include/Utils.h"
namespace Hooks
{
	class Hook_OnGetAttackStaminaCost  //Actor__sub_140627930+16E	call ActorValueOwner__sub_1403BEC90
	{
		/*to cancel out vanilla power attack stamina consumption.*/
	public:
		static void install()
		{
			auto& trampoline = SKSE::GetTrampoline();

			REL::Relocation<uintptr_t> hook{ RELOCATION_ID(37650, 38603) };  //SE:627930 + 16E => 3BEC90 AE:64D350 + 171 => 3D6720

			_getAttackStaminaCost = trampoline.write_call<5>(hook.address() + RELOCATION_OFFSET(0x16E, 0x171), getAttackStaminaCost);
			
			logger::info("hook:OnGetAttackStaminaCost");
		}

	private:
		static float getAttackStaminaCost(RE::ActorValueOwner* avOwner, RE::BGSAttackData* atkData);
		static inline REL::Relocation<decltype(getAttackStaminaCost)> _getAttackStaminaCost;
	};


	class Hook_OnCheckStaminaRegenCondition  //block stamina regen during weapon swing
	{
	public:
		static void install()
		{
			REL::Relocation<uintptr_t> hook{ RELOCATION_ID(37510, 38452) };  // 140620690       140645AA0
			auto& trampoline = SKSE::GetTrampoline();
			_HasFlags1 = trampoline.write_call<5>(hook.address() + RELOCATION_OFFSET(0x62, 0x6F), HasFlags1);
			logger::info("hook:CheckStaminaRegenCondition");
		}

	private:
		static bool HasFlags1(RE::ActorState* a_this, uint16_t a_flags);
		static inline REL::Relocation<decltype(HasFlags1)> _HasFlags1;  //14063C330       140662930
	};

	class Hook_OnRestoreActorValue
	{
	public:
		static void install()
		{
			REL::Relocation<uintptr_t> hook{ RELOCATION_ID(37510, 38452) };// 140620690       140645AA0
			auto& trampoline = SKSE::GetTrampoline();
			_RestoreActorValue = trampoline.write_call<5>(hook.address() + RELOCATION_OFFSET(0x176, 0xE1), RestoreActorValue);
			logger::info("hook:OnRestoresActorValue");
		}

	private:
		static void RestoreActorValue(RE::Actor* a, RE::ActorValue av, float val);
		static inline REL::Relocation<decltype(RestoreActorValue)> _RestoreActorValue; //140620900		140645d30
	};

	class Hook_OnMeleeHit
	{
	public:
		static void install()
		{
			REL::Relocation<uintptr_t> hook{ RELOCATION_ID(37673, 38627) };  //140628C20       14064E760
			auto& trampoline = SKSE::GetTrampoline();
			_ProcessHit = trampoline.write_call<5>(hook.address() + RELOCATION_OFFSET(0x3C0, 0x4A8), processHit);
			logger::info("hook:OnMeleeHit");
		}

	private:
		static void processHit(RE::Actor* victim, RE::HitData& hitData);
		static inline REL::Relocation<decltype(processHit)> _ProcessHit;  //140626400       14064BAB0
	};

	/*Remove vanilla stagger by setting return value of all vanilla stagger magnitude calculations to 0.*/
	//class Hook_OnGetStaggerMagnitude
	//{
	//public:
	//	static void install()
	//	{
	//		auto& trampoline = SKSE::GetTrampoline();
	//		REL::Relocation<uintptr_t> hook1{ REL::ID(42839) };  //Down	p	Character__sub_1407431D0+81	call    ActorValueOwner__sub_1403BEB10
	//		_getStaggerMagnitude_Weapon = trampoline.write_call<5>(hook1.address() + 0x81, getStaggerMagnitude_Weapon);

	//		REL::Relocation<uintptr_t> hook2{ REL::ID(42839) };  //Down	p	Character__sub_1407431D0+5B	call    sub_1403BE760
	//		_getStaggerManitude_Bash = trampoline.write_call<5>(hook2.address() + 0x5B, getStaggerManitude_Bash);
	//		logger::info("hook:OnGetStaggerMagnitude");
	//	}

	//private:
	//	static float getStaggerMagnitude_Weapon(RE::ActorValueOwner* a1, RE::ActorValueOwner* a2, RE::TESObjectWEAP* a3, float a4);
	//	static inline REL::Relocation<decltype(getStaggerMagnitude_Weapon)> _getStaggerMagnitude_Weapon;

	//	static float getStaggerManitude_Bash(uintptr_t a1, uintptr_t a2);
	//	static inline REL::Relocation<decltype(getStaggerManitude_Bash)> _getStaggerManitude_Bash;
	//};

	//class Hook_MagicHit
	//{  //not used now
	//public:
	//	static void install()
	//	{
	//		auto& trampoline = SKSE::GetTrampoline();
	//		REL::Relocation<uintptr_t> hook{ REL::ID(43015) };  // magic hit process
	//		_processMagicHit = trampoline.write_call<5>(hook.address() + 0x216, processMagicHit);
	//		logger::debug("hook:OnMagicHit");
	//	}

	//private:
	//	static void __fastcall processMagicHit([[maybe_unused]] RE::ActorMagicCaster* attacker, [[maybe_unused]] RE::NiPoint3* rdx0, [[maybe_unused]] RE::Projectile* a_projectile, [[maybe_unused]] RE::TESObjectREFR* victim, [[maybe_unused]] float a5, [[maybe_unused]] float a6, [[maybe_unused]] char a7, [[maybe_unused]] char a8)
	//	{
	//		if (!attacker || !victim) {
	//			_processMagicHit(attacker, rdx0, a_projectile, victim, a5, a6, a7, a8);
	//		}
	//		if (victim && victim->IsPlayerRef()) {
	//		}
	//		_processMagicHit(attacker, rdx0, a_projectile, victim, a5, a6, a7, a8);
	//	}
	//	static inline REL::Relocation<decltype(processMagicHit)> _processMagicHit;
	//};

//	class Hook_MainUpdate
//	{
//	public:
//		static void install()
//		{
//			auto& trampoline = SKSE::GetTrampoline();
//			REL::Relocation<uintptr_t> hook{ RELOCATION_ID(35551, 36544) };  // 5AF3D0, 5D29F0, main loop
//
//#ifdef SKYRIM_SUPPORT_AE
//			_Update = trampoline.write_call<5>(hook.address() + 0x160, Update);
//#else
//			_Update = trampoline.write_call<5>(hook.address() + 0x11F, Update);
//#endif
//			logger::info("hook:OnMainUpdate");
//		}
//
//	private:
//		static void Update(RE::Main* a_this, float a2);
//		static inline REL::Relocation<decltype(Update)> _Update;
//	};
	class Hook_AttackBlockHandler_OnProcessButton
	{
	public:
		static void install() 
		{
			REL::Relocation<std::uintptr_t> atkbckVtbl{ RE::VTABLE_AttackBlockHandler[0] };

			// AttackBlockHandler::ProcessButton sits at a different vtable slot depending on the
			// game version: index 0x4 on SE and on AE before 1.7.99, but index 0x6 on AE 1.7.99+,
			// where two new vfuncs (ProcessMotionGesture / ProcessSixaxis) were inserted into
			// RE::PlayerInputHandler ahead of it. See CommonLibSSE-NG's PlayerInputHandler.h:
			//   static constexpr std::size_t kAE1799AddedVFuncCount = 2;
			//   REL::RelocateVirtual<...>(REL::VersionShift(0x04, kAE1799AddedVFuncCount,
			//                                      SKSE::RUNTIME_SSE_1_7_99), 0x04, ...);
			// Hooking the hardcoded 0x4 on 1.7.99+ overwrote an unrelated input vfunc with our
			// ProcessButton and meant the player's block never reached this handler at all, so
			// timed block, perfect block and projectile parry silently did nothing.
			constexpr std::size_t kProcessButtonVtblIndex = 0x4;
			constexpr std::size_t kAE1799AddedVFuncCount = 2;
			const std::size_t processButtonVtblIndex =
				REL::Module::IsAtLeast(SKSE::RUNTIME_SSE_1_7_99) ? kProcessButtonVtblIndex + kAE1799AddedVFuncCount
																 : kProcessButtonVtblIndex;

			_ProcessButton = atkbckVtbl.write_vfunc(processButtonVtblIndex, ProcessButton);
			logger::info("hook:AttackBlockHandler_OnProcessButton (vtable slot = {:#x})", processButtonVtblIndex);
		}

	private:
		static void ProcessButton(RE::AttackBlockHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data);
		static inline REL::Relocation<decltype(ProcessButton)> _ProcessButton;
	};

	class Hook_OnPlayerUpdate  //no longer used
	{
	public:
		static void install()
		{
			REL::Relocation<std::uintptr_t> PlayerCharacterVtbl{ RE::VTABLE_PlayerCharacter[0] };

			_Update = PlayerCharacterVtbl.write_vfunc(0xAD, Update);
			logger::info("hook:OnPlayerUpdate");
		}

	private:
		static void Update(RE::PlayerCharacter* a_this, float a_delta)
		{
			ValhallaCombat::GetSingleton()->update();
			_Update(a_this, a_delta);
		}
		static inline REL::Relocation<decltype(Update)> _Update;
	};

	class Hook_OnProjectileCollision
	{
	public:
		static void install()
		{
			REL::Relocation<std::uintptr_t> arrowProjectileVtbl{ RE::VTABLE_ArrowProjectile[0] };
			REL::Relocation<std::uintptr_t> missileProjectileVtbl{ RE::VTABLE_MissileProjectile[0] };

			_arrowCollission = arrowProjectileVtbl.write_vfunc(190, OnArrowCollision);
			_missileCollission = missileProjectileVtbl.write_vfunc(190, OnMissileCollision);
			logger::info("hook:OnProjectileCollision");
		};

	private:
			static bool OnArrowCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector);

			static bool OnMissileCollision(RE::Projectile* a_this, RE::hkpAllCdPointCollector* a_AllCdPointCollector);
		static inline REL::Relocation<decltype(OnArrowCollision)> _arrowCollission;
		static inline REL::Relocation<decltype(OnMissileCollision)> _missileCollission;
	};

	class Hook_OnMeleeCollision
	{
	public:
		static void install()
		{
			REL::Relocation<uintptr_t> hook{ RELOCATION_ID(37650, 38603) };  //SE:627930 + 38B AE:64D350 +  45A
			auto& trampoline = SKSE::GetTrampoline();

			_ProcessHit = trampoline.write_call<5>(hook.address() + RELOCATION_OFFSET(0x38B, 0x45A), processHit);

			logger::info("hook:OnMeleeCollision");
		}
	private:
		static void processHit(RE::Actor* a_aggressor, RE::Actor* a_victim, std::int64_t a_int1, bool a_bool, void* a_unkptr);
		
		static inline REL::Relocation<decltype(processHit)> _ProcessHit;
	};

	class Hook_GetWantBlock
	{
	public:
		static void install()
		{
			// Upstream left the AE id for this call site as a placeholder (0). CommonLibSSE-NG v10's
			// VariantID/VariantOffset return 0 when the AE value is 0, so write_call would dereference
			// a null address. The SE and AE address libraries use different id numbering, so falling
			// back to the SE id on AE would patch an unrelated function. The last known-good build did
			// not install this hook at all, so skip it when no address is available.
			REL::Relocation<uintptr_t> hook{ RELOCATION_ID(37376, 00000) };
			if (hook.address() == 0) {
				logger::warn("hook:OnGetWantBlock: no AE address supplied upstream; skipping");
				return;
			}
			auto& trampoline = SKSE::GetTrampoline();

			_GetWantBlock = trampoline.write_call<5>(hook.address() + RELOCATION_OFFSET(0x23, 0x00), GetWantBlock);

			logger::info("hook:OnGetWantBlock");
		}

	private:
		static std::int32_t& GetWantBlock(void* unk_ptr, const RE::BSFixedString& a_channelName, std::uint8_t unk_int, RE::Actor* a_actor, std::int32_t& a_result);

		static inline REL::Relocation<decltype(GetWantBlock)> _GetWantBlock;

	};

	class Hook_OnAttackAction
	{
	public:
		static void install()
		{
			auto& trampoline = SKSE::GetTrampoline();
			REL::Relocation<std::uintptr_t> AttackActionBase{ RELOCATION_ID(48139, 49170) };
			_PerformAttackAction = trampoline.write_call<5>(AttackActionBase.address() + RELOCATION_OFFSET(0x4D7, 0x435), PerformAttackAction);
			logger::info("hook:OnAttackAction");
		}

	private:
		static bool PerformAttackAction(RE::TESActionData* a_actionData);

		static inline REL::Relocation<decltype(PerformAttackAction)> _PerformAttackAction;
	};


		static bool install()
	{
		logger::info("Installing hooks...");
			const std::array sites{
				REL::Relocation<std::uintptr_t>{RELOCATION_ID(37650, 38603)}.address() + RELOCATION_OFFSET(0x16E, 0x171),
				REL::Relocation<std::uintptr_t>{RELOCATION_ID(37510, 38452)}.address() + RELOCATION_OFFSET(0x176, 0xE1),
				REL::Relocation<std::uintptr_t>{RELOCATION_ID(37673, 38627)}.address() + RELOCATION_OFFSET(0x3C0, 0x4A8),
				REL::Relocation<std::uintptr_t>{RELOCATION_ID(37650, 38603)}.address() + RELOCATION_OFFSET(0x38B, 0x45A),
				REL::Relocation<std::uintptr_t>{RELOCATION_ID(48139, 49170)}.address() + RELOCATION_OFFSET(0x4D7, 0x435)};
			for (auto site : sites) {
				if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
					logger::error("Hook callsite mismatch at {:#x}; combat hooks were not installed", site);
					return false;
				}
			}
			if (REL::Module::IsSE()) {
				const auto site = REL::Relocation<std::uintptr_t>{REL::ID(37376)}.address() + 0x23;
				if (*reinterpret_cast<const std::uint8_t*>(site) != 0xE8) {
					logger::error("GetWantBlock callsite mismatch at {:#x}; combat hooks were not installed", site);
					return false;
				}
			}
		SKSE::AllocTrampoline(1 << 8);
		Hook_OnGetAttackStaminaCost::install();
		//Hook_OnCheckStaminaRegenCondition::install(); //todo: fix this hook
		Hook_OnRestoreActorValue::install();
		Hook_OnMeleeHit::install();
		Hook_OnPlayerUpdate::install();
		Hook_OnProjectileCollision::install();
		Hook_OnMeleeCollision::install();
		Hook_OnAttackAction::install();
		Hook_GetWantBlock::install();
		Hook_AttackBlockHandler_OnProcessButton::install();
		logger::info("...done");
			return true;
	}
}

