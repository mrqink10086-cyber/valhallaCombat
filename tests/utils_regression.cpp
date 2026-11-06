#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace RE {
enum class ActorValue { kStamina };
struct NiMatrix3 { float entry[3][3]{}; };
struct ActorValueOwner {
	float stamina{};
	float GetActorValue(ActorValue) const { return stamina; }
	void RestoreActorValue(ActorValue, float value) { stamina += value; }
};
struct Actor {
	ActorValueOwner values;
	ActorValueOwner* AsActorValueOwner() { return &values; }
};
}
namespace Utils::Actor {
void damageav(RE::Actor*, RE::ActorValue, float);
bool tryDamageAv(RE::Actor*, RE::ActorValue, float);
}
namespace inlineUtils {
void SetRotationMatrix(RE::NiMatrix3&, float, float, float);
}

#include "utils_functions.inc"

namespace {
void require(bool condition, const char* label)
{
	if (!condition) throw std::runtime_error(label);
	std::cout << "PASS " << label << '\n';
}
void checkRotation(float sacb, float cacb, float sb, const char* label)
{
	RE::NiMatrix3 matrix{};
	inlineUtils::SetRotationMatrix(matrix, sacb, cacb, sb);
	for (const auto& row : matrix.entry) {
		for (float value : row) require(std::isfinite(value), "rotation matrix entries are finite");
	}
	for (int rowA = 0; rowA < 3; ++rowA) {
		for (int rowB = 0; rowB < 3; ++rowB) {
			float dot = 0.0f;
			for (int column = 0; column < 3; ++column) dot += matrix.entry[rowA][column] * matrix.entry[rowB][column];
			const float expected = rowA == rowB ? 1.0f : 0.0f;
			require(std::abs(dot - expected) < 0.0001f, "rotation rows are orthonormal");
		}
	}
	const float determinant =
		matrix.entry[0][0] * (matrix.entry[1][1] * matrix.entry[2][2] - matrix.entry[1][2] * matrix.entry[2][1]) -
		matrix.entry[0][1] * (matrix.entry[1][0] * matrix.entry[2][2] - matrix.entry[1][2] * matrix.entry[2][0]) +
		matrix.entry[0][2] * (matrix.entry[1][0] * matrix.entry[2][1] - matrix.entry[1][1] * matrix.entry[2][0]);
	require(std::abs(determinant - 1.0f) < 0.0001f, "rotation matrix preserves handedness");
	std::cout << "PASS " << label << '\n';
}
}

int main()
{
	try {
		checkRotation(0.0f, 0.0f, 1.0f, "vertical-up input yields a valid rotation");
		checkRotation(0.0f, 0.0f, -1.0f, "vertical-down input yields a valid rotation");
		checkRotation(1.0e-8f, -2.0e-8f, 0.99999994f, "near-vertical input yields a valid rotation");
		checkRotation(0.3f, 0.4f, 0.5f, "general input yields a valid rotation");

		RE::Actor actor;
		actor.values.stamina = 12.0f;
		require(Utils::Actor::tryDamageAv(&actor, RE::ActorValue::kStamina, 12.0f), "actor value equal to cost succeeds");
		require(actor.values.stamina == 0.0f, "equal-cost damage drains the remaining actor value");
		require(Utils::Actor::tryDamageAv(&actor, RE::ActorValue::kStamina, 0.0f), "zero current value permits a zero-cost operation");
		require(actor.values.stamina == 0.0f, "zero current value stays unchanged for zero cost");
		actor.values.stamina = 12.0f;
		require(Utils::Actor::tryDamageAv(&actor, RE::ActorValue::kStamina, 0.0f), "zero-cost damage succeeds");
		require(actor.values.stamina == 12.0f, "zero-cost damage leaves actor value unchanged");
		require(!Utils::Actor::tryDamageAv(&actor, RE::ActorValue::kStamina, -0.01f), "negative actor-value cost is rejected");
		require(actor.values.stamina == 12.0f, "negative cost leaves actor value unchanged");
		require(!Utils::Actor::tryDamageAv(&actor, RE::ActorValue::kStamina, 12.01f), "insufficient actor value rejects the cost");
		require(actor.values.stamina == 12.0f, "rejected cost leaves actor value unchanged");
		require(!Utils::Actor::tryDamageAv(&actor, RE::ActorValue::kStamina, std::nanf("")), "NaN actor-value cost is rejected");
		require(actor.values.stamina == 12.0f, "NaN cost leaves actor value unchanged");
		return 0;
	} catch (const std::exception& exception) {
		std::cerr << "FAIL " << exception.what() << '\n';
		return 1;
	}
}
