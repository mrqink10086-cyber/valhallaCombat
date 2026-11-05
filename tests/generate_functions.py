"""Extract production bodies without rewriting their decisions for host tests."""
from pathlib import Path
import re
import sys

root, output = map(Path, sys.argv[1:])
cases = {
    "apply": ("settings.cpp", ["void settings::apply("]),
    "counter": ("blockHandler.cpp", ["void blockHandler::EldenCounterCompatibility::triggerCounter(", "void blockHandler::EldenCounterCompatibility::async_removeECTriggerSpell(", "void blockHandler::EldenCounterCompatibility::reset()"]),
    "block": ("blockHandler.cpp", ["void blockHandler::onBlockKeyDown()", "void blockHandler::onBlockKeyUp()", "void blockHandler::processStaminaBlock(", "PRECISION_API::PreHitCallbackReturn blockHandler::precisionPrehitCallbackFunc("]),
    "execution": ("executionHandler.cpp", ["void executionHandler::tryPcExecution()", "void executionHandler::queueExecutionIdle(RE::Actor* executor, RE::Actor* victim, RE::TESIdleForm* idle", "void executionHandler::queueExecutionIdle(RE::Actor* executor, RE::Actor* victim, std::vector"]),
    "debuff": ("debuffHandler.cpp", ["void debuffHandler::stopDebuff("]),
    "owned": ("OwnedEffects.cpp", ["void add(", "void remove(", "void retry(", "bool pending()", "void update()"]),
    "message": ("main.cpp", ["void MessageHandler("]),
}
for name, (filename, markers) in cases.items():
    source = (root / "src/bin" / filename).read_text(encoding="utf-8-sig")
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', lambda match: " " * len(match[0]), source, flags=re.S)
    bodies = []
    for marker in markers:
        start = source.index(marker)
        depth = 0
        for end in range(source.index("{", start), len(source)):
            depth += (masked[end] == "{") - (masked[end] == "}")
            if not depth:
                bodies.append(source[start:end + 1])
                break
        else:
            raise ValueError(marker)
    (output / f"{name}_functions.inc").write_text("\n".join(bodies), encoding="utf-8")
stun = (root / "src/bin/stunHandler.cpp").read_text(encoding="utf-8-sig")
(output / "stun_functions.inc").write_text(re.sub(r'^#include[^\n]*\n', '', stun, flags=re.M), encoding="utf-8")
