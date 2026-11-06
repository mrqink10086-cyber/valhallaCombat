"""Extract production bodies without rewriting their decisions for host tests."""
from pathlib import Path
import re
import sys

root, output = map(Path, sys.argv[1:])
cases = {
    "apply": ("settings.cpp", ["void settings::apply("]),
    "counter": ("blockHandler.cpp", ["void blockHandler::EldenCounterCompatibility::triggerCounter(", "void blockHandler::EldenCounterCompatibility::async_removeECTriggerSpell(", "void blockHandler::EldenCounterCompatibility::reset()"]),
    "block": ("blockHandler.cpp", ["void blockHandler::reset()", "void blockHandler::onBlockKeyDown()", "void blockHandler::onBlockKeyUp()", "void blockHandler::processStaminaBlock(", "PRECISION_API::PreHitCallbackReturn blockHandler::precisionPrehitCallbackFunc(", "void blockHandler::update()", "void blockHandler::onPcTimedBlockEnd()", "inline void blockHandler::pcBlockWindowPenalty_update("]),
    "execution": ("executionHandler.cpp", ["void executionHandler::tryPcExecution()", "void executionHandler::queueExecutionIdle(RE::Actor* executor, RE::Actor* victim, RE::TESIdleForm* idle", "void executionHandler::queueExecutionIdle(RE::Actor* executor, RE::Actor* victim, std::vector"]),
    "debuff": ("debuffHandler.cpp", ["void debuffHandler::stopDebuff("]),
    "owned": ("OwnedEffects.cpp", ["void add(", "void remove(", "void retry(", "bool pending()", "void update()"]),
    "message": ("main.cpp", ["void dispatchLifecycle(", "void MessageHandler("]),
    "self_test_trigger": ("CombatSelfTest.cpp", ["void startManualTrigger("]),
}

def extract(source, marker):
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', lambda match: " " * len(match[0]), source, flags=re.S)
    start = source.index(marker)
    depth = 0
    for end in range(source.index("{", start), len(source)):
        depth += (masked[end] == "{") - (masked[end] == "}")
        if not depth:
            return source[start:end + 1]
    raise ValueError(marker)

for name, (filename, markers) in cases.items():
    source = (root / "src/bin" / filename).read_text(encoding="utf-8-sig")
    (output / f"{name}_functions.inc").write_text("\n".join(extract(source, marker) for marker in markers), encoding="utf-8")

combat_sources = [
    ("src/bin/events/animEventHandler.cpp", ["constexpr uint32_t hash(", "constexpr uint32_t operator\"\" _h(", "static void processActor(", "void animEventHandler::reset()", "void animEventHandler::flush()", "void animEventHandler::submit("]),
    ("src/bin/attackHandler.cpp", ["void attackHandler::cacheAttack(", "void attackHandler::OnLightHit(", "void attackHandler::checkout("]),
    ("src/bin/hitProcessor.cpp", ["void hitProcessor::processHit("]),
]
combat = []
for filename, markers in combat_sources:
    source = (root / filename).read_text(encoding="utf-8-sig")
    combat.extend(extract(source, marker) for marker in markers)
attack_header = (root / "src/include/attackHandler.h").read_text(encoding="utf-8-sig")
(output / "combat_functions.inc").write_text("\n".join(combat), encoding="utf-8")
(output / "attack_enabled.inc").write_text(extract(attack_header, "static bool enabledFor("), encoding="utf-8")
(output / "attack_reset.inc").write_text(extract(attack_header, "void reset()"), encoding="utf-8")

attack_source = (root / "src/bin/attackHandler.cpp").read_text(encoding="utf-8-sig")
hooks_source = (root / "src/bin/Hooks.cpp").read_text(encoding="utf-8-sig")
(output / "heavy_attack_functions.inc").write_text("\n".join(extract(attack_source, marker) for marker in [
    "void attackHandler::cacheAttack(", "void attackHandler::checkout("]), encoding="utf-8")
(output / "heavy_hook.inc").write_text(extract(hooks_source, "float Hook_OnGetAttackStaminaCost::getAttackStaminaCost("), encoding="utf-8")

block_source = (root / "src/bin/blockHandler.cpp").read_text(encoding="utf-8-sig")
(output / "projectile.inc").write_text(extract(block_source, "bool blockHandler::queueProjectileBlock("), encoding="utf-8")

utils_cpp = (root / "src/include/Utils.cpp").read_text(encoding="utf-8-sig")
utils_header = (root / "src/include/Utils.h").read_text(encoding="utf-8-sig")
utils_functions = [extract(utils_cpp, "void Utils::Actor::damageav("), extract(utils_cpp, "bool Utils::Actor::tryDamageAv("), "namespace inlineUtils {\n" + extract(utils_header, "inline void SetRotationMatrix(") + "\n}"]
(output / "utils_functions.inc").write_text("\n".join(utils_functions), encoding="utf-8")

stun = (root / "src/bin/stunHandler.cpp").read_text(encoding="utf-8-sig")
(output / "stun_functions.inc").write_text(re.sub(r'^#include[^\n]*\n', '', stun, flags=re.M), encoding="utf-8")
