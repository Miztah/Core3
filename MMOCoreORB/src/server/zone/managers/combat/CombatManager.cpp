/*
 * CombatManager.cpp
 *
 *  Created on: 24/02/2010
 *      Author: victor
 */

#include "CombatManager.h"
#include "CreatureAttackData.h"
#include "server/zone/objects/scene/variables/DeltaVector.h"
#include "server/zone/objects/creature/CreatureObject.h"
#include "server/zone/objects/player/PlayerObject.h"
#include "server/zone/objects/player/events/LogoutTask.h"
#include "server/zone/objects/creature/CreatureState.h"
#include "server/zone/objects/creature/commands/CombatQueueCommand.h"
#include "server/zone/objects/creature/CreatureAttribute.h"
#include "server/zone/packets/object/CombatSpam.h"
#include "server/zone/packets/object/CombatAction.h"
#include "server/zone/packets/chat/ChatSystemMessage.h"
#include "server/zone/packets/tangible/UpdatePVPStatusMessage.h"
#include "server/zone/Zone.h"
#include "server/zone/managers/collision/CollisionManager.h"
#include "server/zone/objects/creature/buffs/StateBuff.h"
#include "server/zone/managers/visibility/VisibilityManager.h"
#include "server/zone/managers/creature/CreatureManager.h"
#include "server/zone/managers/creature/LairObserver.h"
#include "server/zone/managers/reaction/ReactionManager.h"
#include "server/zone/objects/installation/components/TurretDataComponent.h"
#include "server/zone/objects/creature/ai/AiAgent.h"

#define COMBAT_SPAM_RANGE 85

const uint32 CombatManager::defaultAttacks[9] = {
		0x99476628, 0xF5547B91, 0x3CE273EC, 0x734C00C,
		0x43C4FFD0, 0x56D7CC78, 0x4B41CAFB, 0x2257D06B,
		0x306887EB
};


bool CombatManager::startCombat(CreatureObject* attacker, TangibleObject* defender, bool lockDefender) {
	if (attacker == defender)
		return false;

	if (attacker->getZone() == NULL || defender->getZone() == NULL)
		return false;

	if (attacker->isRidingMount()) {
		ManagedReference<CreatureObject*> parent = attacker->getParent().get().castTo<CreatureObject*>();

		if (parent == NULL || !parent->isMount())
			return false;

		if (attacker->hasBuff(STRING_HASHCODE("gallop")))
			return false;
	}

	if (attacker->hasRidingCreature())
		return false;

	if (!defender->isAttackableBy(attacker))
		return false;

	if (defender->isCreatureObject() && defender->asCreatureObject()->isIncapacitated())
		return false;

	if (attacker->isPlayerCreature() && attacker->getPlayerObject()->isAFK())
		return false;

	attacker->clearState(CreatureState::PEACE);

	Locker clocker(defender, attacker);

	attacker->setDefender(defender);
	defender->addDefender(attacker);

	return true;
}

bool CombatManager::attemptPeace(CreatureObject* attacker) {
	DeltaVector<ManagedReference<SceneObject*> >* defenderList = attacker->getDefenderList();

	for (int i = defenderList->size() - 1; i >= 0; --i) {
		try {
			ManagedReference<SceneObject*> object = defenderList->get(i);

			TangibleObject* defender = cast<TangibleObject*>( object.get());

			if (defender == NULL)
				continue;

			try {
				Locker clocker(defender, attacker);

				if (defender->hasDefender(attacker)) {

					if (defender->isCreatureObject()) {
						CreatureObject* creature = defender->asCreatureObject();

						if (creature->getMainDefender() != attacker || creature->hasState(CreatureState::PEACE) || creature->isDead() || attacker->isDead() || !creature->isInRange(attacker, 128.f)) {
							attacker->removeDefender(defender);
							defender->removeDefender(attacker);
						}
					} else {
						attacker->removeDefender(defender);
						defender->removeDefender(attacker);
					}
				} else {
					attacker->removeDefender(defender);
				}

				clocker.release();

			} catch (Exception& e) {
				error(e.getMessage());
				e.printStackTrace();
			}
		} catch (ArrayIndexOutOfBoundsException& exc) {

		}
	}

	if (defenderList->size() != 0) {
		//info("defenderList not empty, trying to set Peace State");

		attacker->setState(CreatureState::PEACE);

		return false;
	} else {
		attacker->clearCombatState(false);

		// clearCombatState() (rightfully) does not automatically set peace, so set it
		attacker->setState(CreatureState::PEACE);

		return true;
	}
}

void CombatManager::forcePeace(CreatureObject* attacker) {
	assert(attacker->isLockedByCurrentThread());

	DeltaVector<ManagedReference<SceneObject*> >* defenderList = attacker->getDefenderList();

	for (int i = 0; i < defenderList->size(); ++i) {
		ManagedReference<SceneObject*> object = defenderList->getSafe(i);

		if (object == NULL || !object->isTangibleObject())
			continue;

		TangibleObject* defender = cast<TangibleObject*>( object.get());

		Locker clocker(defender, attacker);

		if (defender->hasDefender(attacker)) {
			attacker->removeDefender(defender);
			defender->removeDefender(attacker);
		} else {
			attacker->removeDefender(defender);
		}

		--i;

		clocker.release();
	}

	attacker->clearCombatState(false);
	attacker->setState(CreatureState::PEACE);
}



int CombatManager::doCombatAction(CreatureObject* attacker, WeaponObject* weapon, TangibleObject* defenderObject, CombatQueueCommand* command) {
	return doCombatAction(attacker, weapon, defenderObject, CreatureAttackData("",command));
}

int CombatManager::doCombatAction(CreatureObject* attacker, WeaponObject* weapon, TangibleObject* defenderObject, const CreatureAttackData& data) {
	//info("entering doCombat action with data ", true);

	if (!startCombat(attacker, defenderObject))
		return -1;

	//info("past start combat", true);

	if (attacker->hasAttackDelay() || !attacker->checkPostureChangeDelay())
		return -3;

	//info("past delay", true);

	if (!applySpecialAttackCost(attacker, weapon, data))
		return -2;

	//info("past special attack cost", true);

	int damage = 0;

	if (data.getCommand()->isAreaAction() || data.getCommand()->isConeAction())
		damage = doAreaCombatAction(attacker, weapon, defenderObject, data);
	else
		damage = doTargetCombatAction(attacker, weapon, defenderObject, data);

	if (damage > 0) {
		attacker->updateLastSuccessfulCombatAction();

		if (attacker->isPlayerCreature() && data.getCommandCRC() != STRING_HASHCODE("attack"))
			weapon->decay(attacker);
	}

	return damage;
}

int CombatManager::doCombatAction(TangibleObject* attacker, WeaponObject* weapon, TangibleObject* defender, CombatQueueCommand* command){

	if(weapon != NULL){
		if(!command->isAreaAction()){
			return doTargetCombatAction(attacker, weapon, defender, CreatureAttackData("",command));
		} else {
			return doAreaCombatAction(attacker, weapon, defender, CreatureAttackData("",command));
		}
	}

	return 0;
}

int CombatManager::doTargetCombatAction(CreatureObject* attacker, WeaponObject* weapon, TangibleObject* tano, const CreatureAttackData& data) {
	int damage = 0;

	Locker clocker(tano, attacker);

	if (!tano->isAttackableBy(attacker))
		return 0;

	attacker->addDefender(tano);
	tano->addDefender(attacker);

	if (tano->isCreatureObject()) {
		CreatureObject* defender = tano->asCreatureObject();

		if (defender->getWeapon() == NULL)
			return 0;

		damage = doTargetCombatAction(attacker, weapon, defender, data);
	} else {
		int poolsToDamage = calculatePoolsToDamage(data.getPoolsToDamage());

		damage = applyDamage(attacker, weapon, tano, poolsToDamage, data);

		broadcastCombatAction(attacker, tano, weapon, data, 0x01);

		data.getCommand()->sendAttackCombatSpam(attacker, tano, HIT, damage, data);
	}

	tano->notifyObservers(ObserverEventType::DAMAGERECEIVED, attacker, damage);

	if (damage > 0 && tano->isAiAgent()) {
		AiAgent* aiAgent = cast<AiAgent*>(tano);
		bool help = false;

		for (int i = 0; i < 9; i += 3) {
			if (aiAgent->getHAM(i) < (aiAgent->getMaxHAM(i) / 2)) {
				help = true;
				break;
			}
		}

		if (help)
			aiAgent->sendReactionChat(ReactionManager::HELP);
		else
			aiAgent->sendReactionChat(ReactionManager::HIT);
	}

	if (damage > 0 && attacker->isAiAgent()) {
		AiAgent* aiAgent = cast<AiAgent*>(attacker);
		aiAgent->sendReactionChat(ReactionManager::HITTARGET);
	}

	return damage;
}

int CombatManager::doTargetCombatAction(CreatureObject* attacker, WeaponObject* weapon, CreatureObject* defender, const CreatureAttackData& data) {
	if (defender->isEntertaining())
		defender->stopEntertaining();

	int hitVal = HIT;
	float damageMultiplier = data.getDamageMultiplier();

	// need to calculate damage here to get proper client spam
	int damage = 0;
	Vector<int> foodMitigation; //used to save the food mitigation value for combat spam sent later.

	if (damageMultiplier != 0)
		damage = calculateDamage(attacker, weapon, defender, data, foodMitigation) * damageMultiplier;

	damageMultiplier = 1.0f;

	if (!data.isStateOnlyAttack()) {
		hitVal = getHitChance(attacker, defender, weapon, damage, data.getAccuracyBonus() + attacker->getSkillMod(data.getCommand()->getAccuracySkillMod()));

		//Send Attack Combat Spam. For state-only attacks, this is sent in applyStates().
		data.getCommand()->sendAttackCombatSpam(attacker, defender, hitVal, damage, data);
	}

	broadcastCombatAction(attacker, defender, weapon, data, hitVal);

	switch (hitVal) {
	case MISS:
		doMiss(attacker, weapon, defender, damage);
		return 0;
		break;
	case HIT:
		break;
	case BLOCK:
		doBlock(attacker, weapon, defender, damage);
		damageMultiplier = 0.5f;
		break;
	case DODGE:
		doDodge(attacker, weapon, defender, damage);
		damageMultiplier = 0.0f;
		break;
	case COUNTER: {
		doCounterAttack(attacker, weapon, defender, damage);
		if (!defender->hasState(CreatureState::PEACE))
			defender->executeObjectControllerAction(STRING_HASHCODE("attack"), attacker->getObjectID(), "");
		damageMultiplier = 0.0f;
		break;}
	case RICOCHET:
		doLightsaberBlock(attacker, weapon, defender, damage);
		damageMultiplier = 0.0f;
		break;
	default:
		break;
	}

	// Apply states first
	applyStates(attacker, defender, data);

	// Return if it's a state only attack (intimidate, warcry, wookiee roar) so they don't apply dots or break combat delays
	if (data.isStateOnlyAttack()) {
		return 0;
	}

	if (defender->hasAttackDelay())
		defender->removeAttackDelay();

	if (damageMultiplier != 0 && damage != 0) {
		int poolsToDamage = calculatePoolsToDamage(data.getPoolsToDamage()); // TODO: animations are probably determined by which pools are damaged (high, mid, low, combos, etc)
		int unmitDamage = damage;
		damage = applyDamage(attacker, weapon, defender, damage, damageMultiplier, poolsToDamage, data);

		applyDots(attacker, defender, data, damage, unmitDamage, poolsToDamage);
		applyWeaponDots(attacker, defender, weapon);
	}

	//Send defensive buff combat spam last.
	if (!foodMitigation.isEmpty())
		sendMitigationCombatSpam(defender, weapon, foodMitigation.get(0), FOOD);

	return damage;
}

int CombatManager::doTargetCombatAction(TangibleObject* attacker, WeaponObject* weapon, TangibleObject* tano, const CreatureAttackData& data) {

	int damage = 0;

	Locker clocker(tano, attacker);

	if (tano->isCreatureObject()) {
		CreatureObject* defenderObject = tano->asCreatureObject();

		if (defenderObject->getWeapon() != NULL)
			damage = doTargetCombatAction(attacker, weapon, defenderObject, data);
	} else {
		// TODO: implement, tano->tano damage
	}

	return damage;
}

int CombatManager::doTargetCombatAction(TangibleObject* attacker, WeaponObject* weapon, CreatureObject* defenderObject, const CreatureAttackData& data) {
	if(defenderObject == NULL || !defenderObject->isAttackableBy(attacker))
		return 0;

	if (defenderObject->isEntertaining())
		defenderObject->stopEntertaining();

	attacker->addDefender(defenderObject);
	defenderObject->addDefender(attacker);

	float damageMultiplier = data.getDamageMultiplier();

	// need to calculate damage here to get proper client spam
	int damage = 0;
	Vector<int> foodMitigation; //used to save the food mitigation value for combat spam sent later.

	if (damageMultiplier != 0)
		damage = calculateDamage(attacker, weapon, defenderObject, data, foodMitigation) * damageMultiplier;

	damageMultiplier = 1.0f;
	int hitVal = getHitChance(attacker, defenderObject, weapon, damage, data.getAccuracyBonus());

	//Send Attack Combat Spam
	data.getCommand()->sendAttackCombatSpam(attacker, defenderObject, hitVal, damage, data);

	CombatAction* combatAction = NULL;

	uint32 animationCRC = data.getAnimationCRC();
	combatAction = new CombatAction(attacker, defenderObject, animationCRC, hitVal, CombatManager::DEFAULTTRAIL);
	attacker->broadcastMessage(combatAction,true);

	switch (hitVal) {
	case MISS:
		doMiss(attacker, weapon, defenderObject, damage);
		return 0;
		break;
	case HIT:
		break;
	case BLOCK:
		doBlock(attacker, weapon, defenderObject, damage);
		damageMultiplier = 0.5f;
		break;
	case DODGE:
		doDodge(attacker, weapon, defenderObject, damage);
		damageMultiplier = 0.0f;
		break;
	case COUNTER:
		doCounterAttack(attacker, weapon, defenderObject, damage);
		if (!defenderObject->hasState(CreatureState::PEACE))
			defenderObject->executeObjectControllerAction(STRING_HASHCODE("attack"), attacker->getObjectID(), "");
		damageMultiplier = 0.0f;
		break;
	case RICOCHET:
		doLightsaberBlock(attacker, weapon, defenderObject, damage);
		damageMultiplier = 0.0f;
		break;
	default:
		break;
	}

	int poolsToDamage = calculatePoolsToDamage(data.getPoolsToDamage());

	if (poolsToDamage == 0)
		return 0;

	if (defenderObject->hasAttackDelay())
		defenderObject->removeAttackDelay();

	if (damageMultiplier != 0 && damage != 0)
		damage = applyDamage(attacker, weapon, defenderObject, damage, damageMultiplier, poolsToDamage, data);

	//Send defensive buff combat spam last.
	if (!foodMitigation.isEmpty())
		sendMitigationCombatSpam(defenderObject, weapon, foodMitigation.get(0), FOOD);

	return damage;
}

void CombatManager::applyDots(CreatureObject* attacker, CreatureObject* defender, const CreatureAttackData& data, int appliedDamage, int unmitDamage, int poolsToDamage) {
	VectorMap<uint64, DotEffect>* dotEffects = data.getDotEffects();

	if (defender->isPlayerCreature() && defender->getPvpStatusBitmask() == CreatureFlag::NONE)
		return;

	for (int i = 0; i < dotEffects->size(); i++) {
		DotEffect effect = dotEffects->get(i);

		if (defender->hasDotImmunity(effect.getDotType()) || effect.getDotDuration() == 0 || System::random(100) > effect.getDotChance())
			continue;

		Vector<String> defenseMods = effect.getDefenderStateDefenseModifers();
		int resist = 0;

		for (int j = 0; j < defenseMods.size(); j++)
			resist += defender->getSkillMod(defenseMods.get(j));

		int damageToApply = appliedDamage;
		uint32 dotType = effect.getDotType();

		if (effect.isDotDamageofHit()) {
			// determine if we should use unmitigated damage
			if (dotType != CreatureState::BLEEDING)
				damageToApply = unmitDamage;
		}

		int potency = effect.getDotPotency();

		if (potency == 0) {
			potency = 150;
		}

		uint8 pool = effect.getDotPool();

		if (pool == CreatureAttribute::UNKNOWN) {
			pool = getPoolForDot(dotType, poolsToDamage);
		}

		//info("entering addDotState", true);
		defender->addDotState(attacker, dotType, data.getCommand()->getNameCRC(), effect.isDotDamageofHit() ? damageToApply * effect.getPrimaryPercent() / 100.0f : effect.getDotStrength(), pool, effect.getDotDuration(), potency, resist,
				effect.isDotDamageofHit() ? damageToApply * effect.getSecondaryPercent() / 100.0f : effect.getDotStrength());
	}
}

void CombatManager::applyWeaponDots(CreatureObject* attacker, CreatureObject* defender, WeaponObject* weapon) {
	if (defender->getPvpStatusBitmask() == CreatureFlag::NONE)
		return;

	if (!weapon->isCertifiedFor(attacker))
		return;

	int resist = defender->getSkillMod("combat_bleeding_defense");

	for (int i = 0; i < weapon->getNumberOfDots(); i++) {
		if (weapon->getDotUses(i) <= 0)
			continue;

		int type = 0;

		switch (weapon->getDotType(i)) {
		case 1: //POISON
			type = CreatureState::POISONED;
			break;
		case 2: //DISEASE
			type = CreatureState::DISEASED;
			break;
		case 3: //FIRE
			type = CreatureState::ONFIRE;
			break;
		case 4: //BLEED
			type = CreatureState::BLEEDING;
			break;
		default:
			break;
		}

		if (defender->hasDotImmunity(type))
			continue;

		if (weapon->getDotPotency(i)*(1.f-resist/100.f) > System::random(100) && defender->addDotState(attacker, type, weapon->getObjectID(), weapon->getDotStrength(i), weapon->getDotAttribute(i), weapon->getDotDuration(i), -1, 0, weapon->getDotStrength(i)) > 0) // Unresisted, reduce use count.
			if (weapon->getDotUses(i) > 0) weapon->setDotUses(weapon->getDotUses(i) - 1, i);
	}
}

uint8 CombatManager::getPoolForDot(uint64 dotType, int poolsToDamage) {
	uint8 pool = 0;

	switch (dotType) {
	case CreatureState::POISONED:
	case CreatureState::ONFIRE:
	case CreatureState::BLEEDING:
		if (poolsToDamage & HEALTH) {
			pool = CreatureAttribute::HEALTH;
		} else if (poolsToDamage & ACTION) {
			pool = CreatureAttribute::ACTION;
		} else if (poolsToDamage & MIND) {
			pool = CreatureAttribute::MIND;
		}
		break;
	case CreatureState::DISEASED:
		if (poolsToDamage & HEALTH) {
			pool = CreatureAttribute::HEALTH + System::random(2);
		} else if (poolsToDamage & ACTION) {
			pool = CreatureAttribute::ACTION + System::random(2);
		} else if (poolsToDamage & MIND) {
			pool = CreatureAttribute::MIND + System::random(2);
		}
		break;
	default:
		break;
	}

	return pool;
}

float CombatManager::getWeaponRangeModifier(float currentRange, WeaponObject* weapon) {
	float minRange = 0;
	float idealRange = 2;
	float maxRange = 5;

	float smallMod = 7;
	float bigMod = 7;

	minRange = (float) weapon->getPointBlankRange();
	idealRange = (float) weapon->getIdealRange();
	maxRange = (float) weapon->getMaxRange();

	smallMod = (float) weapon->getPointBlankAccuracy();
	bigMod = (float) weapon->getIdealAccuracy();

	if (currentRange >= maxRange)
		return (float) weapon->getMaxRangeAccuracy();

	if (currentRange <= minRange)
		return smallMod;

	// this assumes that we are attacking somewhere between point blank and ideal range
	float smallRange = minRange;
	float bigRange = idealRange;

	// check that assumption and correct if it's not true
	if (currentRange > idealRange) {
		smallMod = (float) weapon->getIdealAccuracy();
		bigMod = (float) weapon->getMaxRangeAccuracy();

		smallRange = idealRange;
		bigRange = maxRange;
	}

	if (bigRange == smallRange) // if they are equal, we know at least one is ideal, so just return the ideal accuracy mod
		return weapon->getIdealAccuracy();

	return smallMod + ((currentRange - smallRange) / (bigRange - smallRange) * (bigMod - smallMod));
}

int CombatManager::calculatePostureModifier(CreatureObject* creature, WeaponObject* weapon) {
	CreaturePosture* postureLookup = CreaturePosture::instance();

	uint8 locomotion = creature->getLocomotion();

	if (!weapon->isMeleeWeapon())
		return postureLookup->getRangedAttackMod(locomotion);
	else
		return postureLookup->getMeleeAttackMod(locomotion);
}

int CombatManager::calculateTargetPostureModifier(WeaponObject* weapon, CreatureObject* targetCreature) {
	CreaturePosture* postureLookup = CreaturePosture::instance();

	uint8 locomotion = targetCreature->getLocomotion();

	if (!weapon->isMeleeWeapon())
		return postureLookup->getRangedDefenseMod(locomotion);
	else
		return postureLookup->getMeleeDefenseMod(locomotion);
}

int CombatManager::getAttackerAccuracyModifier(TangibleObject* attacker, CreatureObject* defender, WeaponObject* weapon) {
	if (attacker->isAiAgent()) {
		return cast<AiAgent*>(attacker)->getChanceHit() * 100;
	} else if (attacker->isInstallationObject()) {
		return cast<InstallationObject*>(attacker)->getHitChance() * 100;
	}

	if (!attacker->isCreatureObject()) {
		return 0;
	}

	CreatureObject* creoAttacker = cast<CreatureObject*>(attacker);

	int attackerAccuracy = 0;

	Vector<String>* creatureAccMods = weapon->getCreatureAccuracyModifiers();

	for (int i = 0; i < creatureAccMods->size(); ++i) {
		String mod = creatureAccMods->get(i);
		attackerAccuracy += creoAttacker->getSkillMod(mod);
		attackerAccuracy += creoAttacker->getSkillMod("private_" + mod);

		if (creoAttacker->isStanding()) {
			attackerAccuracy += creoAttacker->getSkillMod(mod + "_while_standing");
		}
	}

	if (attackerAccuracy == 0) attackerAccuracy = -15; // unskilled penalty, TODO: this might be -50 or -125, do research

	attackerAccuracy += creoAttacker->getSkillMod("attack_accuracy") + creoAttacker->getSkillMod("dead_eye");

	// now apply overall weapon defense mods
	if (weapon->isMeleeWeapon()) {
		switch (defender->getWeapon()->getGameObjectType()) {
		case SceneObjectType::PISTOL:
			attackerAccuracy += 20.f;
			/* no break */
		case SceneObjectType::CARBINE:
			attackerAccuracy += 55.f;
			/* no break */
		case SceneObjectType::RIFLE:
		case SceneObjectType::MINE:
		case SceneObjectType::SPECIALHEAVYWEAPON:
		case SceneObjectType::HEAVYWEAPON:
			attackerAccuracy += 25.f;
		}
	}

	return attackerAccuracy;
}

int CombatManager::getAttackerAccuracyBonus(CreatureObject* attacker, WeaponObject* weapon) {
	int bonus = 0;

	bonus += attacker->getSkillMod("private_attack_accuracy");
	bonus += attacker->getSkillMod("private_accuracy_bonus");

	if (weapon->getAttackType() == WeaponObject::MELEEATTACK)
		bonus += attacker->getSkillMod("private_melee_accuracy_bonus");
	if (weapon->getAttackType() == WeaponObject::RANGEDATTACK)
		bonus += attacker->getSkillMod("private_ranged_accuracy_bonus");

	return bonus;
}

int CombatManager::getDefenderDefenseModifier(CreatureObject* defender, WeaponObject* weapon, TangibleObject* attacker) {
	if (!defender->isPlayerCreature())
		return MIN(125, defender->getLevel());

	int targetDefense = 0;
	int buffDefense = 0;

	Vector<String>* defenseAccMods = weapon->getDefenderDefenseModifiers();

	for (int i = 0; i < defenseAccMods->size(); ++i) {
		String mod = defenseAccMods->get(i);
		targetDefense += defender->getSkillMod(mod);
		targetDefense += defender->getSkillMod("private_" + mod);
	}

	//info("Base target defense is " + String::valueOf(targetDefense), true);

	// defense hardcap
	if (targetDefense > 125)
		targetDefense = 125;

	if (attacker->isPlayerCreature())
		targetDefense += defender->getSkillMod("private_defense");

	// SL bonuses go on top of hardcap
	for (int i = 0; i < defenseAccMods->size(); ++i) {
		String mod = defenseAccMods->get(i);
		targetDefense += defender->getSkillMod("private_group_" + mod);
	}

	// food bonus goes on top as well
	targetDefense += defender->getSkillMod("dodge_attack");
	targetDefense += defender->getSkillMod("private_dodge_attack");

	//info("Target defense after state affects and cap is " +  String::valueOf(targetDefense), true);

	return targetDefense;
}

int CombatManager::getDefenderSecondaryDefenseModifier(CreatureObject* defender) {
	if (defender->isIntimidated() || defender->isBerserked()) return 0;

	int targetDefense = 0;
	ManagedReference<WeaponObject*> weapon = defender->getWeapon();

	Vector<String>* defenseAccMods = weapon->getDefenderSecondaryDefenseModifiers();

	for (int i = 0; i < defenseAccMods->size(); ++i) {
		String mod = defenseAccMods->get(i);
		targetDefense += defender->getSkillMod(mod);
		targetDefense += defender->getSkillMod("private_" + mod);
	}

	if (targetDefense > 125)
		targetDefense = 125;

	return targetDefense;
}

float CombatManager::getDefenderToughnessModifier(CreatureObject* defender, int attackType, int damType, float damage, Vector<int>& foodMitigation) {
	ManagedReference<WeaponObject*> weapon = defender->getWeapon();

	Vector<String>* defenseToughMods = weapon->getDefenderToughnessModifiers();

	if (attackType == weapon->getAttackType()) {
		for (int i = 0; i < defenseToughMods->size(); ++i) {
			int toughMod = defender->getSkillMod(defenseToughMods->get(i));
			if (toughMod > 0) damage *= 1.f - (toughMod / 100.f);
		}
	}

	int jediToughness = defender->getSkillMod("jedi_toughness");
	if (damType != WeaponObject::LIGHTSABER && jediToughness > 0)
		damage *= 1.f - (jediToughness / 100.f);

	float foodMitigatedDamage = damage;
	int foodBonus = defender->getSkillMod("mitigate_damage");
	if (foodBonus > 0) {
		damage *= 1.f - (foodBonus / 100.f);
		foodMitigation.add((int)(foodMitigatedDamage -= damage)); //save value for later combat spam
	}

	return damage < 0 ? 0 : damage;
}

float CombatManager::hitChanceEquation(float attackerAccuracy, float attackerRoll, float targetDefense, float defenderRoll) {
	float roll = (attackerRoll - defenderRoll)/50;
	int8 rollSign = (roll > 0) - (roll < 0);

	float accTotal = 75.f + (float)roll;

	for (int i = 1; i <= 4; i++) {
		if (roll*rollSign > i) {
			accTotal += (float)rollSign*25.f;
			roll -= rollSign*i;
		} else {
			accTotal += roll/((float)i)*25.f;
			break;
		}
	}

	accTotal += attackerAccuracy - targetDefense;

	/*StringBuffer msg;
	msg << "HitChance\n";
	msg << "\tTarget Defense " << targetDefense << "\n";
	msg << "\tTarget Defense Bonus" << defenseBonus << "\n";

	info(msg.toString());*/

	return accTotal;
}
int CombatManager::calculateDamageRange(TangibleObject* attacker, CreatureObject* defender, WeaponObject* weapon) {
	int attackType = weapon->getAttackType();
	int damageMitigation = 0;
	float minDamage = weapon->getMinDamage(), maxDamage = weapon->getMaxDamage();

	// restrict damage if a player is not certified (don't worry about mobs)
	if (attacker->isPlayerCreature() && !weapon->isCertifiedFor(cast<CreatureObject*>(attacker))) {
		minDamage = 5;
		maxDamage = 10;
	}

	//info("attacker base damage is " + String::valueOf(minDamage) + "-"+ String::valueOf(maxDamage), true);

	PlayerObject* defenderGhost = defender->getPlayerObject();

	// this is for damage mitigation
	if (defenderGhost != NULL) {
		String mitString;
		switch (attackType){
		case WeaponObject::MELEEATTACK:
			mitString = "melee_damage_mitigation_";
			break;
		case WeaponObject::RANGEDATTACK:
			mitString = "ranged_damage_mitigation_";
			break;
		default:
			break;
		}

		for (int i = 3; i > 0; i--) {
			if (defenderGhost->hasAbility(mitString + i)) {
				damageMitigation = i;
				break;
			}
		}

		if (damageMitigation > 0)
			maxDamage = minDamage + (maxDamage - minDamage) * (1 - (0.2 * damageMitigation));
	}

	float range = maxDamage - minDamage;

	//info("attacker weapon damage mod is " + String::valueOf(maxDamage), true);

	return range < 0 ? 0 : (int)range;
}

float CombatManager::applyDamageModifiers(CreatureObject* attacker, WeaponObject* weapon, float damage, const CreatureAttackData& data) {
	if (data.getAttackType() == CombatManager::WEAPONATTACK) {
		Vector<String>* weaponDamageMods = weapon->getDamageModifiers();

		for (int i = 0; i < weaponDamageMods->size(); ++i) {
			damage += attacker->getSkillMod(weaponDamageMods->get(i));
		}

		if (weapon->getAttackType() == WeaponObject::MELEEATTACK)
			damage += attacker->getSkillMod("private_melee_damage_bonus");
		if (weapon->getAttackType() == WeaponObject::RANGEDATTACK)
			damage += attacker->getSkillMod("private_ranged_damage_bonus");
	}

	damage += attacker->getSkillMod("private_damage_bonus");

	int damageMultiplier = attacker->getSkillMod("private_damage_multiplier");

	if (damageMultiplier != 0)
		damage *= damageMultiplier;

	int damageDivisor = attacker->getSkillMod("private_damage_divisor");

	if (damageDivisor != 0)
		damage /= damageDivisor;

	return damage;
}

int CombatManager::getSpeedModifier(CreatureObject* attacker, WeaponObject* weapon) {
	int speedMods = 0;

	Vector<String>* weaponSpeedMods = weapon->getSpeedModifiers();

	for (int i = 0; i < weaponSpeedMods->size(); ++i) {
		speedMods += attacker->getSkillMod(weaponSpeedMods->get(i));
	}

	speedMods += attacker->getSkillMod("private_speed_bonus");

	if (weapon->getAttackType() == WeaponObject::MELEEATTACK)
		speedMods += attacker->getSkillMod("private_melee_speed_bonus");
	if (weapon->getAttackType() == WeaponObject::RANGEDATTACK)
		speedMods += attacker->getSkillMod("private_ranged_speed_bonus");

	return speedMods;
}



int CombatManager::getArmorObjectReduction(ArmorObject* armor, int damageType) {
	float resist = 0;

	switch (damageType) {
	case WeaponObject::KINETIC:
		resist = armor->getKinetic();
		break;
	case WeaponObject::ENERGY:
		resist = armor->getEnergy();
		break;
	case WeaponObject::ELECTRICITY:
		resist = armor->getElectricity();
		break;
	case WeaponObject::STUN:
		resist = armor->getStun();
		break;
	case WeaponObject::BLAST:
		resist = armor->getBlast();
		break;
	case WeaponObject::HEAT:
		resist = armor->getHeat();
		break;
	case WeaponObject::COLD:
		resist = armor->getCold();
		break;
	case WeaponObject::ACID:
		resist = armor->getAcid();
		break;
	case WeaponObject::LIGHTSABER:
		resist = armor->getLightSaber();
		break;
	}

	return MAX(0, (int)resist);
}

ArmorObject* CombatManager::getHealthArmor(CreatureObject* defender) {
	Vector<ManagedReference<ArmorObject*> > healthArmor = defender->getWearablesDeltaVector()->getArmorAtHitLocation(CombatManager::CHEST);

	if (System::random(1) == 0)
		healthArmor = defender->getWearablesDeltaVector()->getArmorAtHitLocation(CombatManager::ARMS);

	ManagedReference<ArmorObject*> armorToHit = NULL;

	if (!healthArmor.isEmpty())
		armorToHit = healthArmor.get(System::random(healthArmor.size() - 1));

	return armorToHit;
}

ArmorObject* CombatManager::getActionArmor(CreatureObject* defender) {
	Vector<ManagedReference<ArmorObject*> > actionArmor = defender->getWearablesDeltaVector()->getArmorAtHitLocation(CombatManager::LEGS);

	ManagedReference<ArmorObject*> armorToHit = NULL;

	if (!actionArmor.isEmpty())
		armorToHit = actionArmor.get(System::random(actionArmor.size() - 1));

	return armorToHit;
}

ArmorObject* CombatManager::getMindArmor(CreatureObject* defender) {
	Vector<ManagedReference<ArmorObject*> > mindArmor = defender->getWearablesDeltaVector()->getArmorAtHitLocation(CombatManager::HEAD);

	ManagedReference<ArmorObject*> armorToHit = NULL;

	if (!mindArmor.isEmpty())
		armorToHit = mindArmor.get(System::random(mindArmor.size() - 1));

	return armorToHit;
}

ArmorObject* CombatManager::getPSGArmor(CreatureObject* defender) {
	SceneObject* psg = defender->getSlottedObject("utility_belt");

	if (psg != NULL && psg->isPsgArmorObject())
		return cast<ArmorObject*>(psg);

	return NULL;
}

int CombatManager::getArmorNpcReduction(AiAgent* defender, int damageType) {
	float resist = 0;

	switch (damageType) {
	case WeaponObject::KINETIC:
		resist = defender->getKinetic();
		break;
	case WeaponObject::ENERGY:
		resist = defender->getEnergy();
		break;
	case WeaponObject::ELECTRICITY:
		resist = defender->getElectricity();
		break;
	case WeaponObject::STUN:
		resist = defender->getStun();
		break;
	case WeaponObject::BLAST:
		resist = defender->getBlast();
		break;
	case WeaponObject::HEAT:
		resist = defender->getHeat();
		break;
	case WeaponObject::COLD:
		resist = defender->getCold();
		break;
	case WeaponObject::ACID:
		resist = defender->getAcid();
		break;
	case WeaponObject::LIGHTSABER:
		resist = defender->getLightSaber();
		break;
	}

	return (int)resist;
}

int CombatManager::getArmorVehicleReduction(VehicleObject* defender, int damageType) {
	float resist = 0;

	switch (damageType) {
	case WeaponObject::KINETIC:
		resist = defender->getKinetic();
		break;
	case WeaponObject::ENERGY:
		resist = defender->getEnergy();
		break;
	case WeaponObject::ELECTRICITY:
		resist = defender->getElectricity();
		break;
	case WeaponObject::STUN:
		resist = defender->getStun();
		break;
	case WeaponObject::BLAST:
		resist = defender->getBlast();
		break;
	case WeaponObject::HEAT:
		resist = defender->getHeat();
		break;
	case WeaponObject::COLD:
		resist = defender->getCold();
		break;
	case WeaponObject::ACID:
		resist = defender->getAcid();
		break;
	case WeaponObject::LIGHTSABER:
		resist = defender->getLightSaber();
		break;
	}

	return (int)resist;
}

int CombatManager::getArmorReduction(TangibleObject* attacker, WeaponObject* weapon, CreatureObject* defender, float damage, int poolToDamage, const CreatureAttackData& data) {
	int damageType = 0, armorPiercing = 1;

	if (data.getAttackType() == CombatManager::WEAPONATTACK) {
		damageType = weapon->getDamageType();
		armorPiercing = weapon->getArmorPiercing();

		if (weapon->isBroken())
			armorPiercing = 0;
	} else {
		damageType = data.getDamageType();
	}

	if (defender->isAiAgent()) {
		float armorReduction = getArmorNpcReduction(cast<AiAgent*>(defender), damageType);

		if (armorReduction >= 0)
			damage *= getArmorPiercing(cast<AiAgent*>(defender), armorPiercing);

		if (armorReduction > 0) damage *= (1.f - (armorReduction / 100.f));

		return damage;
	} else if (defender->isVehicleObject()) {
		float armorReduction = getArmorVehicleReduction(cast<VehicleObject*>(defender), damageType);

		if (armorReduction >= 0)
			damage *= getArmorPiercing(cast<VehicleObject*>(defender), armorPiercing);

		if (armorReduction > 0) damage *= (1.f - (armorReduction / 100.f));

		return damage;
	}

	if (data.getAttackType() == CombatManager::WEAPONATTACK) {
		// Force Armor
		float rawDamage = damage;

		int forceArmor = defender->getSkillMod("force_armor");
		if (forceArmor > 0) {
			float dmgAbsorbed = rawDamage - (damage *= 1.f - (forceArmor / 100.f));
			defender->notifyObservers(ObserverEventType::FORCEBUFFHIT, attacker, dmgAbsorbed);
			sendMitigationCombatSpam(defender, NULL, (int)dmgAbsorbed, FORCEARMOR);
		}
	} else if (data.getAttackType() == CombatManager::FORCEATTACK) {
		float jediBuffDamage = 0;
		float rawDamage = damage;

		// Force Shield
		int forceShield = defender->getSkillMod("force_shield");
		if (forceShield > 0) {
			jediBuffDamage = rawDamage - (damage *= 1.f - (forceShield / 100.f));
			sendMitigationCombatSpam(defender, NULL, (int)jediBuffDamage, FORCESHIELD);
		}

		// Force Feedback
		int forceFeedback = defender->getSkillMod("force_feedback");
		if (forceFeedback > 0 && (defender->hasBuff(BuffCRC::JEDI_FORCE_FEEDBACK_1) || defender->hasBuff(BuffCRC::JEDI_FORCE_FEEDBACK_2))) {
			float feedbackDmg = rawDamage * (forceFeedback / 100.f);
			float splitDmg = feedbackDmg / 3;

			attacker->inflictDamage(defender, CreatureAttribute::HEALTH, splitDmg, true);
			attacker->inflictDamage(defender, CreatureAttribute::ACTION, splitDmg, true);
			attacker->inflictDamage(defender, CreatureAttribute::MIND, splitDmg, true);
			broadcastCombatSpam(defender, attacker, NULL, feedbackDmg, "cbt_spam", "forcefeedback_hit", 1);
			defender->playEffect("clienteffect/pl_force_feedback_block.cef", "");
		}

		// Force Absorb
		if (defender->getSkillMod("force_absorb") > 0 && defender->isPlayerCreature()) {
			ManagedReference<PlayerObject*> playerObject = defender->getPlayerObject();
			if (playerObject != NULL) {
				playerObject->setForcePower(playerObject->getForcePower() + (damage * 0.5));
				sendMitigationCombatSpam(defender, NULL, (int)damage * 0.5, FORCEABSORB);
				defender->playEffect("clienteffect/pl_force_absorb_hit.cef", "");
			}
		}

		defender->notifyObservers(ObserverEventType::FORCEBUFFHIT, attacker, jediBuffDamage);
	}

	// PSG
	ManagedReference<ArmorObject*> psg = getPSGArmor(defender);

	if (psg != NULL && !psg->isVulnerable(damageType)) {
		float armorReduction =  getArmorObjectReduction(psg, damageType);
		float dmgAbsorbed = damage;

        if (armorReduction > 0) damage *= 1.f - (armorReduction / 100.f);

		dmgAbsorbed -= damage;
		if (dmgAbsorbed > 0)
			sendMitigationCombatSpam(defender, psg, (int)dmgAbsorbed, PSG);

		// inflict condition damage
		// TODO: this formula makes PSG's take more damage than regular armor, but that's how it was on live
		// it can be fixed by doing condition damage after all damage reductions

		Locker plocker(psg);

		psg->inflictDamage(psg, 0, damage * 0.1, true, true);

	}

	// Standard Armor
	ManagedReference<ArmorObject*> armor = NULL;

	if (poolToDamage & CombatManager::HEALTH)
		armor = getHealthArmor(defender);
	else if (poolToDamage & CombatManager::ACTION)
		armor = getActionArmor(defender);
	else if (poolToDamage & CombatManager::MIND)
		armor = getMindArmor(defender);

	if (armor != NULL && !armor->isVulnerable(damageType)) {
		float armorReduction = getArmorObjectReduction(armor, damageType);
		float dmgAbsorbed = damage;

		// use only the damage applied to the armor for piercing (after the PSG takes some off)
		damage *= getArmorPiercing(armor, armorPiercing);

		if (armorReduction > 0) {
			damage *= (1.f - (armorReduction / 100.f));
			dmgAbsorbed -= damage;
			sendMitigationCombatSpam(defender, armor, (int)dmgAbsorbed, ARMOR);
		}

		// inflict condition damage
		Locker alocker(armor);

		armor->inflictDamage(armor, 0, damage * 0.1, true, true);
	}

	return damage;
}

float CombatManager::getArmorPiercing(TangibleObject* defender, int armorPiercing) {
	int armorReduction = 0;

	if (defender->isAiAgent()) {
		AiAgent* aiDefender = cast<AiAgent*>(defender);
		armorReduction = aiDefender->getArmor();
	} else if (defender->isArmorObject()) {
		ArmorObject* armorDefender = cast<ArmorObject*>(defender);

		if (armorDefender != NULL && !armorDefender->isBroken())
			armorReduction = armorDefender->getRating();
	} else if (defender->isVehicleObject()) {
		VehicleObject* vehicleDefender = cast<VehicleObject*>(defender);
		armorReduction = vehicleDefender->getArmor();
	} else {
		DataObjectComponentReference* data = defender->getDataObjectComponent();

		if (data != NULL) {
			TurretDataComponent* turretData = cast<TurretDataComponent*>(data->get());

			if(turretData != NULL) {
				armorReduction = turretData->getArmorRating();
			}
		}
	}

	if (armorPiercing > armorReduction)
		return pow(1.25, armorPiercing - armorReduction);
    else
        return pow(0.50, armorReduction - armorPiercing);
}

float CombatManager::calculateDamage(CreatureObject* attacker, WeaponObject* weapon, TangibleObject* defender, const CreatureAttackData& data) {
	float damage = 0;
	int diff = 0;

	if (data.getMinDamage() > 0 || data.getMaxDamage() > 0) { // this is a special attack (force, etc)
		damage = data.getMinDamage();
		diff = data.getMaxDamage() - damage;

	} else {
		float minDamage = weapon->getMinDamage(), maxDamage = weapon->getMaxDamage();

		if (attacker->isPlayerCreature() && !weapon->isCertifiedFor(attacker)) {
			minDamage = 5.f;
			maxDamage = 10.f;
		}

		damage = minDamage;
		diff = maxDamage - minDamage;
	}

	if (diff > 0)
		damage += System::random(diff);

	damage = applyDamageModifiers(attacker, weapon, damage, data);

	if (attacker->isPlayerCreature())
		damage *= 1.5;

	if (data.getAttackType() == CombatManager::WEAPONATTACK && weapon->getAttackType() == WeaponObject::MELEEATTACK)
		damage *= 1.25;

	//info("damage to be dealt is " + String::valueOf(damage), true);

	ManagedReference<LairObserver*> lairObserver = NULL;
	SortedVector<ManagedReference<Observer*> > observers = defender->getObservers(ObserverEventType::OBJECTDESTRUCTION);

	for (int i = 0; i < observers.size(); i++) {
		lairObserver = cast<LairObserver*>(observers.get(i).get());
		if (lairObserver != NULL)
			break;
	}

	if (lairObserver && lairObserver->getSpawnNumber() > 2)
		damage *= 3.5;

	return damage;
}

float CombatManager::doDroidDetonation(CreatureObject* droid, CreatureObject* defender, float damage) {
	if (defender->isPlayerCreature() && defender->getPvpStatusBitmask() == CreatureFlag::NONE) {
		return 0;
	}
	if (defender->isCreatureObject()) {
		if (defender->isPlayerCreature())
			damage *= 0.25;
		// pikc a pool to target
		int pool = calculatePoolsToDamage(RANDOM);
		// we now have damage to use lets apply it
		float healthDamage = 0.f, actionDamage = 0.f, mindDamage = 0.f;
		// need to check armor reduction with just defender, blast and their AR + resists
		if(defender->isVehicleObject()) {
			int ar = cast<VehicleObject*>(defender)->getBlast();
			if ( ar > 0)
				damage *= (1.f - (ar / 100.f));
			healthDamage = damage;
			actionDamage = damage;
			mindDamage = damage;
		} else if (defender->isAiAgent()) {
			int ar = cast<AiAgent*>(defender)->getBlast();
			if ( ar > 0)
				damage *= (1.f - (ar / 100.f));
			healthDamage = damage;
			actionDamage = damage;
			mindDamage = damage;

		} else {
			// player
			ArmorObject* healthArmor = getHealthArmor(defender);
			ArmorObject* mindArmor = getMindArmor(defender);
			ArmorObject* actionArmor = getActionArmor(defender);
			ArmorObject* psgArmor = getPSGArmor(defender);
			if (psgArmor != NULL && !psgArmor->isVulnerable(WeaponObject::BLAST)) {
				float armorReduction =  psgArmor->getBlast();
				if (armorReduction > 0) damage *= (1.f - (armorReduction / 100.f));

				Locker plocker(psgArmor);

				psgArmor->inflictDamage(psgArmor, 0, damage * 0.1, true, true);
			}
			// reduced by psg not check each spot for damage
			healthDamage = damage;
			actionDamage = damage;
			mindDamage = damage;
			if (healthArmor != NULL && !healthArmor->isVulnerable(WeaponObject::BLAST) && (pool & HEALTH)) {
				float armorReduction = healthArmor->getBlast();
				if (armorReduction > 0)
					healthDamage *= (1.f - (armorReduction / 100.f));

				Locker hlocker(healthArmor);

				healthArmor->inflictDamage(healthArmor, 0, healthDamage * 0.1, true, true);
				return (int)healthDamage * 0.1;
			}
			if (mindArmor != NULL && !mindArmor->isVulnerable(WeaponObject::BLAST) && (pool & MIND)) {
				float armorReduction = mindArmor->getBlast();
				if (armorReduction > 0)
					mindDamage *= (1.f - (armorReduction / 100.f));

				Locker mlocker(mindArmor);

				mindArmor->inflictDamage(mindArmor, 0, mindDamage * 0.1, true, true);
				return (int)mindDamage * 0.1;
			}
			if (actionArmor != NULL && !actionArmor->isVulnerable(WeaponObject::BLAST) && (pool & ACTION)) {
				float armorReduction = actionArmor->getBlast();
				if (armorReduction > 0)
					actionDamage *= (1.f - (armorReduction / 100.f));

				Locker alocker(actionArmor);

				actionArmor->inflictDamage(actionArmor, 0, actionDamage * 0.1, true, true);
				return (int)actionDamage * 0.1;
			}
		}
		if((pool & ACTION)){
			defender->inflictDamage(droid, CreatureAttribute::ACTION, (int)actionDamage, true, true);
			return (int)actionDamage;
		}
		if((pool & HEALTH)) {
			defender->inflictDamage(droid, CreatureAttribute::HEALTH, (int)healthDamage, true, true);
			return (int)healthDamage;
		}
		if((pool & MIND)) {
			defender->inflictDamage(droid, CreatureAttribute::MIND, (int)mindDamage, true, true);
			return (int)mindDamage;
		}
		return 0;
	} else {
		return 0;
	}
}

float CombatManager::calculateDamage(CreatureObject* attacker, WeaponObject* weapon, CreatureObject* defender, const CreatureAttackData& data, Vector<int>& foodMitigation) {
	float damage = 0;
	int diff = 0;

	if (data.getMinDamage() > 0 || data.getMaxDamage() > 0) { // this is a special attack (force, etc)
		damage = data.getMinDamage();
		diff = data.getMaxDamage() - damage;

	} else {
		diff = calculateDamageRange(attacker, defender, weapon);
		float minDamage = weapon->getMinDamage();

		if (attacker->isPlayerCreature() && !weapon->isCertifiedFor(attacker))
			minDamage = 5;

		damage = minDamage;
	}

	if (diff > 0)
		damage += System::random(diff);

	damage = applyDamageModifiers(attacker, weapon, damage, data);

	damage += defender->getSkillMod("private_damage_susceptibility");

	if (attacker->isPlayerCreature())
		damage *= 1.5;

	if (data.getAttackType() == CombatManager::WEAPONATTACK && weapon->getAttackType() == WeaponObject::MELEEATTACK)
		damage *= 1.25;

	if (defender->isKnockedDown())
		damage *= 1.5f;

	// Toughness reduction
	if (data.getAttackType() == CombatManager::FORCEATTACK)
		damage = getDefenderToughnessModifier(defender, WeaponObject::FORCEATTACK, data.getDamageType(), damage, foodMitigation);
	else
		damage = getDefenderToughnessModifier(defender, weapon->getAttackType(), weapon->getDamageType(), damage, foodMitigation);

	// PvP Damage Reduction.
	if (attacker->isPlayerCreature() && defender->isPlayerCreature()) {
		damage *= 0.25;
	}

	if (damage < 1) damage = 1;

	//info("damage to be dealt is " + String::valueOf(damage), true);

	return damage;
}

float CombatManager::calculateDamage(TangibleObject* attacker, WeaponObject* weapon, CreatureObject* defender, const CreatureAttackData& data, Vector<int>& foodMitigation) {
	float damage = 0;

	int diff = calculateDamageRange(attacker, defender, weapon);
	float minDamage = weapon->getMinDamage();

	if (diff > 0)
		damage = System::random(diff) + (int)minDamage;

	damage += defender->getSkillMod("private_damage_susceptibility");

	if (defender->isKnockedDown())
		damage *= 1.5f;

	// Toughness reduction
	damage = getDefenderToughnessModifier(defender, weapon->getAttackType(), weapon->getDamageType(), damage, foodMitigation);

	return damage;
}

int CombatManager::getHitChance(TangibleObject* attacker, CreatureObject* targetCreature, WeaponObject* weapon, int damage, int accuracyBonus) {
	int hitChance = 0;
	int attackType = weapon->getAttackType();
	CreatureObject* creoAttacker = NULL;

	if (attacker->isCreatureObject()) {
		creoAttacker = attacker->asCreatureObject();
	}

	//info("Calculating hit chance for " + attacker->getDisplayedName(), true);
	//info("Attacker accuracy bonus is " + String::valueOf(accuracyBonus), true);
	float weaponAccuracy = 0.0f;
	// Get the weapon mods for range and add the mods for stance
	weaponAccuracy = getWeaponRangeModifier(attacker->getDistanceTo(targetCreature) - targetCreature->getTemplateRadius() - attacker->getTemplateRadius(), weapon);
	// accounts for steadyaim, general aim, and specific weapon aim, these buffs will clear after a completed combat action
	if (creoAttacker != NULL) {
		if (weapon->getAttackType() == WeaponObject::RANGEDATTACK) weaponAccuracy += creoAttacker->getSkillMod("private_aim");
	}
	//info("Attacker weapon accuracy is " + String::valueOf(weaponAccuracy), true);

	int attackerAccuracy = getAttackerAccuracyModifier(attacker, targetCreature, weapon);
	//info("Base attacker accuracy is " + String::valueOf(attackerAccuracy), true);

	// need to also add in general attack accuracy (mostly gotten from posture and states)

	int bonusAccuracy = 0;
	if (creoAttacker != NULL)
		bonusAccuracy = getAttackerAccuracyBonus(creoAttacker, weapon);

	// this is the scout/ranger creature hit bonus that only works against creatures (not NPCS)
	if (targetCreature->isCreature() && creoAttacker != NULL) {
		bonusAccuracy += creoAttacker->getSkillMod("creature_hit_bonus");
	}
	//info("Attacker total bonus is " + String::valueOf(bonusAccuracy), true);

	int postureAccuracy = 0;

	if (creoAttacker != NULL)
		postureAccuracy = calculatePostureModifier(creoAttacker, weapon);

	//info("Attacker posture accuracy is " + String::valueOf(postureAccuracy), true);

	int targetDefense = getDefenderDefenseModifier(targetCreature, weapon, attacker);
	//info("Defender defense is " + String::valueOf(targetDefense), true);

	int postureDefense = calculateTargetPostureModifier(weapon, targetCreature);
	//info("Defender posture defense is " + String::valueOf(postureDefense), true);
	float attackerRoll = (float)System::random(249) + 1.f;
	float defenderRoll = (float)System::random(150) + 25.f;

	// TODO (dannuic): add the trapmods in here somewhere (defense down trapmods)
	float accTotal = hitChanceEquation(attackerAccuracy + weaponAccuracy + accuracyBonus + postureAccuracy + bonusAccuracy, attackerRoll, targetDefense + postureDefense, defenderRoll);

	//info("Final hit chance is " + String::valueOf(accTotal), true);

	if (System::random(100) > accTotal) // miss, just return MISS
		return MISS;

	//info("Attack hit successfully", true);

	// now we have a successful hit, so calculate secondary defenses if there is a damage component
	if (damage > 0) {
		ManagedReference<WeaponObject*> targetWeapon = targetCreature->getWeapon();
		Vector<String>* defenseAccMods = targetWeapon->getDefenderSecondaryDefenseModifiers();
		String def = defenseAccMods->get(0); // FIXME: this is hacky, but a lot faster than using contains()

		// saber block is special because it's just a % chance to block based on the skillmod
		if (def == "saber_block") {
			if (!attacker->isTurret() && (weapon->getAttackType() == WeaponObject::RANGEDATTACK) && ((System::random(100)) < targetCreature->getSkillMod(def)))
				return RICOCHET;
			else return HIT;
		}

		targetDefense = getDefenderSecondaryDefenseModifier(targetCreature);

		//info("Secondary defenses are " + String::valueOf(targetDefense), true);

		if (targetDefense <= 0)
			return HIT; // no secondary defenses

		// add in a random roll
		targetDefense += System::random(199) + 1;

		//TODO: posture defense (or a simplified version thereof: +10 standing, -20 prone, 0 crouching) might be added in to this calculation, research this
		//TODO: dodge and counterattack might get a  +25 bonus (even when triggered via DA), research this

		int cobMod = targetCreature->getSkillMod("private_center_of_being");
		//info("Center of Being mod is " + String::valueOf(cobMod), true);

		targetDefense += cobMod;
		//info("Final modified secondary defense is " + String::valueOf(targetDefense), true);

		if (targetDefense > 50 + attackerAccuracy + weaponAccuracy + accuracyBonus + postureAccuracy + bonusAccuracy + attackerRoll) { // successful secondary defense, return type of defense

			//info("Secondaries defenses prevailed", true);
			// this means use defensive acuity, which mean random 1, 2, or 3
			if (targetWeapon == NULL)
				return System::random(2) + 1;

			if (def == "block")
				return BLOCK;
			else if (def == "dodge")
				return DODGE;
			else if (def == "counterattack")
				return COUNTER;
			else if (def == "unarmed_passive_defense")
				return System::random(2) + 1;
			else // shouldn't get here
				return HIT; // no secondary defenses available on this weapon
		}
	}

	return HIT;
}

float CombatManager::calculateWeaponAttackSpeed(CreatureObject* attacker, WeaponObject* weapon, float skillSpeedRatio) {
	int speedMod = getSpeedModifier(attacker, weapon);
	float jediSpeed = attacker->getSkillMod("combat_haste") / 100.0f;

	float attackSpeed = (1.0f - ((float) speedMod / 100.0f)) * skillSpeedRatio * weapon->getAttackSpeed();

	if (jediSpeed > 0)
		attackSpeed = attackSpeed * jediSpeed;

	return MAX(attackSpeed, 1.0f);
}

void CombatManager::doMiss(TangibleObject* attacker, WeaponObject* weapon, CreatureObject* defender, int damage) {
	defender->showFlyText("combat_effects", "miss", 0xFF, 0xFF, 0xFF);

}

void CombatManager::doCounterAttack(TangibleObject* attacker, WeaponObject* weapon, CreatureObject* defender, int damage) {
	defender->showFlyText("combat_effects", "counterattack", 0, 0xFF, 0);
	//defender->doCombatAnimation(defender, STRING_HASHCODE("dodge"), 0);

}

void CombatManager::doBlock(TangibleObject* attacker, WeaponObject* weapon, CreatureObject* defender, int damage) {
	defender->showFlyText("combat_effects", "block", 0, 0xFF, 0);

	//defender->doCombatAnimation(defender, STRING_HASHCODE("dodge"), 0);

}

void CombatManager::doLightsaberBlock(TangibleObject* attacker, WeaponObject* weapon, CreatureObject* defender, int damage) {
	// No Fly Text.

	//creature->doCombatAnimation(defender, STRING_HASHCODE("test_sword_ricochet"), 0);

}

void CombatManager::doDodge(TangibleObject* attacker, WeaponObject* weapon, CreatureObject* defender, int damage) {
	defender->showFlyText("combat_effects", "dodge", 0, 0xFF, 0);

	//defender->doCombatAnimation(defender, STRING_HASHCODE("dodge"), 0);

}

bool CombatManager::applySpecialAttackCost(CreatureObject* attacker, WeaponObject* weapon, const CreatureAttackData& data) {
	if (attacker->isAiAgent() || data.getAttackType() != CombatManager::WEAPONATTACK)
		return true;

	float force = weapon->getForceCost() * data.getForceCostMultiplier();

	if (force > 0) { // Need Force check first otherwise it can be spammed.
		ManagedReference<PlayerObject*> playerObject = attacker->getPlayerObject();
		if (playerObject != NULL) {
			if (playerObject->getForcePower() <= force) {
				attacker->sendSystemMessage("@jedi_spam:no_force_power");
				return false;
			} else {
				playerObject->setForcePower(playerObject->getForcePower() - force);
			}
		}
	}

	float health = weapon->getHealthAttackCost() * data.getHealthCostMultiplier();
	float action = weapon->getActionAttackCost() * data.getActionCostMultiplier();
	float mind = weapon->getMindAttackCost() * data.getMindCostMultiplier();

	health = attacker->calculateCostAdjustment(CreatureAttribute::STRENGTH, health);
	action = attacker->calculateCostAdjustment(CreatureAttribute::QUICKNESS, action);
	mind = attacker->calculateCostAdjustment(CreatureAttribute::FOCUS, mind);

	if (attacker->getHAM(CreatureAttribute::HEALTH) <= health)
		return false;

	if (attacker->getHAM(CreatureAttribute::ACTION) <= action)
		return false;

	if (attacker->getHAM(CreatureAttribute::MIND) <= mind)
		return false;

	if (health > 0)
		attacker->inflictDamage(attacker, CreatureAttribute::HEALTH, health, true);

	if (action > 0)
		attacker->inflictDamage(attacker, CreatureAttribute::ACTION, action, true);

	if (mind > 0)
		attacker->inflictDamage(attacker, CreatureAttribute::MIND, mind, true);

	return true;
}

void CombatManager::applyStates(CreatureObject* creature, CreatureObject* targetCreature, const CreatureAttackData& data) {
	VectorMap<uint8, StateEffect>* stateEffects = data.getStateEffects();
	int stateAccuracyBonus = data.getStateAccuracyBonus();

	if (targetCreature->isPlayerCreature() && targetCreature->getPvpStatusBitmask() == CreatureFlag::NONE)
		return;

	int playerLevel = 0;
	if (targetCreature->isPlayerCreature()) {
		ZoneServer* server = targetCreature->getZoneServer();
		if (server != NULL) {
			PlayerManager* pManager = server->getPlayerManager();
			if (pManager != NULL) {
				playerLevel = pManager->calculatePlayerLevel(targetCreature) - 5;
			}
		}
	}

	// loop through all the states in the command
	for (int i = 0; i < stateEffects->size(); i++) {
		StateEffect effect = stateEffects->get(i);
		bool failed = false;
		uint8 effectType = effect.getEffectType();

		float accuracyMod = effect.getStateChance() + stateAccuracyBonus;
		if (data.isStateOnlyAttack())
			accuracyMod += creature->getSkillMod(data.getCommand()->getAccuracySkillMod());

		//Check for state immunity.
		if (targetCreature->hasEffectImmunity(effectType))
			failed = true;

		if(!failed) {
			Vector<String> exclusionTimers = effect.getDefenderExclusionTimers();
			// loop through any exclusion timers
			for (int j = 0; j < exclusionTimers.size(); j++)
				if (!targetCreature->checkCooldownRecovery(exclusionTimers.get(j))) failed = true;
		}

		float targetDefense = 0.f;

		// if recovery timer conditions aren't satisfied, it won't matter
		if (!failed) {
			Vector<String> defenseMods = effect.getDefenderStateDefenseModifiers();
			// add up all defenses against the state the target has
			for (int j = 0; j < defenseMods.size(); j++)
				targetDefense += targetCreature->getSkillMod(defenseMods.get(j));

			targetDefense -= targetCreature->calculateBFRatio();
			targetDefense /= 1.5;
			targetDefense += playerLevel;

			if (System::random(100) > accuracyMod - targetDefense)
				failed = true;

			// no reason to apply jedi defenses if primary defense was successful
			if (!failed) {
				targetDefense = 0.f;
				Vector<String> jediMods = effect.getDefenderJediStateDefenseModifiers();
				// second chance for jedi, roll against their special defense "jedi_state_defense"
				for (int j = 0; j < jediMods.size(); j++)
					targetDefense += targetCreature->getSkillMod(jediMods.get(j));

				targetDefense /= 1.5;
				targetDefense += playerLevel;

				if (System::random(100) > accuracyMod - targetDefense)
					failed = true;
			}
		}

		if (!failed) {
			if (effectType == CommandEffect::NEXTATTACKDELAY) {
				StringIdChatParameter stringId("combat_effects", "delay_applied_other");
				stringId.setTT(targetCreature);
				stringId.setDI(effect.getStateLength());
				creature->sendSystemMessage(stringId);
			}

			data.getCommand()->applyEffect(targetCreature, effectType, effect.getStateStrength() + stateAccuracyBonus, data.getCommandCRC());
		}

		// can move this to scripts, but only these states have fail messages
		if (failed) {
			switch (effectType) {
			case CommandEffect::KNOCKDOWN:
				if (!targetCreature->checkKnockdownRecovery() && targetCreature->getPosture() != CreaturePosture::UPRIGHT)
					targetCreature->setPosture(CreaturePosture::UPRIGHT);
				creature->sendSystemMessage("@cbt_spam:knockdown_fail");
				break;
			case CommandEffect::POSTUREDOWN:
				if (!targetCreature->checkPostureDownRecovery() && targetCreature->getPosture() != CreaturePosture::UPRIGHT)
					targetCreature->setPosture(CreaturePosture::UPRIGHT);
				creature->sendSystemMessage("@cbt_spam:posture_change_fail");
				break;
			case CommandEffect::POSTUREUP:
				if (!targetCreature->checkPostureUpRecovery() && targetCreature->getPosture() != CreaturePosture::UPRIGHT)
					targetCreature->setPosture(CreaturePosture::UPRIGHT);
				creature->sendSystemMessage("@cbt_spam:posture_change_fail");
				break;
			case CommandEffect::NEXTATTACKDELAY:
				targetCreature->showFlyText("combat_effects", "warcry_miss", 0xFF, 0, 0 );
				break;
			case CommandEffect::INTIMIDATE:
				targetCreature->showFlyText("combat_effects", "intimidated_miss", 0xFF, 0, 0 );
				break;
			default:
				break;
			}
		}

		// now check combat equilibrium
		if (!failed && (effectType == CommandEffect::KNOCKDOWN || effectType == CommandEffect::POSTUREDOWN || effectType == CommandEffect::POSTUREUP)) {
			int combatEquil = targetCreature->getSkillMod("combat_equillibrium");

			if (combatEquil > 100)
				combatEquil = 100;

			if ((combatEquil >> 1) > (int) System::random(100) && !targetCreature->isDead() && !targetCreature->isIntimidated())
				targetCreature->setPosture(CreaturePosture::UPRIGHT, true);
		}

		//Send Combat Spam for state-only attacks.
		if (data.isStateOnlyAttack()) {
			if (failed)
				data.getCommand()->sendAttackCombatSpam(creature, targetCreature, MISS, 0, data);
			else
				data.getCommand()->sendAttackCombatSpam(creature, targetCreature, HIT, 0, data);
		}
	}

}

int CombatManager::calculatePoolsToDamage(int poolsToDamage) {
	if (poolsToDamage & RANDOM) {
		int rand = System::random(100);

		if (rand < 50) {
			poolsToDamage = HEALTH;
		} else if (rand < 85) {
			poolsToDamage = ACTION;
		} else {
			poolsToDamage = MIND;
		}
	}

	return poolsToDamage;
}

int CombatManager::applyDamage(TangibleObject* attacker, WeaponObject* weapon, CreatureObject* defender, int damage, float damageMultiplier, int poolsToDamage, const CreatureAttackData& data) {
	if (poolsToDamage == 0 || damageMultiplier == 0)
		return 0;

	float ratio = weapon->getWoundsRatio();
	float healthDamage = 0.f, actionDamage = 0.f, mindDamage = 0.f;

	if (defender->isPlayerCreature() && defender->getPvpStatusBitmask() == CreatureFlag::NONE) {
		return 0;
	}

	String xpType;
	if (data.getAttackType() == CombatManager::FORCEATTACK)
		xpType = "jedi_general";
	else if (attacker->isPet())
		xpType = "creaturehandler";
	else
		xpType = weapon->getXpType();

	if (poolsToDamage & HEALTH) {
		healthDamage = getArmorReduction(attacker, weapon, defender, damage * data.getHealthDamageMultiplier(), HEALTH, data) * damageMultiplier;
		defender->inflictDamage(attacker, CreatureAttribute::HEALTH, (int)healthDamage, true, xpType);

		if (System::random(100) < ratio)
			defender->addWounds(CreatureAttribute::HEALTH, 1, true);

		if (System::random(100) < ratio)
			defender->addWounds(CreatureAttribute::STRENGTH, 1, true);

		if (System::random(100) < ratio)
			defender->addWounds(CreatureAttribute::CONSTITUTION, 1, true);
	}

	if (poolsToDamage & ACTION) {
		actionDamage = getArmorReduction(attacker, weapon, defender, damage * data.getActionDamageMultiplier(), ACTION, data) * damageMultiplier;
		defender->inflictDamage(attacker, CreatureAttribute::ACTION, (int)actionDamage, true, xpType);

		if (System::random(100) < ratio)
			defender->addWounds(CreatureAttribute::ACTION, 1, true);

		if (System::random(100) < ratio)
			defender->addWounds(CreatureAttribute::QUICKNESS, 1, true);

		if (System::random(100) < ratio)
			defender->addWounds(CreatureAttribute::STAMINA, 1, true);
	}

	if (poolsToDamage & MIND) {
		mindDamage = getArmorReduction(attacker, weapon, defender, damage * data.getMindDamageMultiplier(), MIND, data) * damageMultiplier;
		defender->inflictDamage(attacker, CreatureAttribute::MIND, (int)mindDamage, true, xpType);

		if (System::random(100) < ratio)
			defender->addWounds(CreatureAttribute::MIND, 1, true);

		if (System::random(100) < ratio)
			defender->addWounds(CreatureAttribute::FOCUS, 1, true);

		if (System::random(100) < ratio)
			defender->addWounds(CreatureAttribute::WILLPOWER, 1, true);
	}

	// now splash damage
	float maxDamage = MAX(healthDamage, MAX(actionDamage, mindDamage));
	if ((poolsToDamage ^ 0x7) & HEALTH)
		defender->inflictDamage(attacker, CreatureAttribute::HEALTH, (int)(maxDamage/10.f + 0.5f), true, xpType);
	if ((poolsToDamage ^ 0x7) & ACTION)
		defender->inflictDamage(attacker, CreatureAttribute::ACTION, (int)(maxDamage/10.f + 0.5f), true, xpType);
	if ((poolsToDamage ^ 0x7) & MIND)
		defender->inflictDamage(attacker, CreatureAttribute::MIND, (int)(maxDamage/10.f + 0.5f), true, xpType);

	// This method can be called multiple times for area attacks. Let the calling method decrease the powerup once
	if (data.getAttackType() == CombatManager::WEAPONATTACK && !data.getCommand()->isAreaAction() && !data.getCommand()->isConeAction() && attacker->isCreatureObject()) {
		weapon->decreasePowerupUses(attacker->asCreatureObject());
	}

	return (int) (healthDamage + actionDamage + mindDamage);
}

int CombatManager::applyDamage(CreatureObject* attacker, WeaponObject* weapon, TangibleObject* defender, int poolsToDamage, const CreatureAttackData& data) {
	if (poolsToDamage == 0)
		return 0;

	if (defender->isPlayerCreature() && defender->getPvpStatusBitmask() == CreatureFlag::NONE) {
		return 0;
	}

	int damage = calculateDamage(attacker, weapon, defender, data);

	float damageMultiplier = data.getDamageMultiplier();

	if (damageMultiplier != 0)
		damage *= damageMultiplier;

	String xpType;
	if (data.getAttackType() == CombatManager::FORCEATTACK)
		xpType = "jedi_general";
	else if (attacker->isPet())
		xpType = "creaturehandler";
	else
		xpType = weapon->getXpType();

	if (defender->isTurret()) {
		int damageType = 0, armorPiercing = 1;

		if (data.getAttackType() == CombatManager::WEAPONATTACK) {
			damageType = weapon->getDamageType();
			armorPiercing = weapon->getArmorPiercing();

			if (weapon->isBroken())
				armorPiercing = 0;
		} else {
			damageType = data.getDamageType();
		}

		int armorReduction = getArmorTurretReduction(attacker, defender, damageType);

		if (armorReduction >= 0)
			damage *= getArmorPiercing(defender, armorPiercing);

		damage *= (1.f - (armorReduction / 100.f));
	}

	defender->inflictDamage(attacker, 0, damage, true, xpType);

	// This method can be called multiple times for area attacks. Let the calling method decrease the powerup once
	if (data.getAttackType() == CombatManager::WEAPONATTACK && !data.getCommand()->isAreaAction() && !data.getCommand()->isConeAction())
		weapon->decreasePowerupUses(attacker);

	return damage;
}

void CombatManager::sendMitigationCombatSpam(CreatureObject* defender, TangibleObject* item, uint32 damage, int type) {
	if (defender == NULL || !defender->isPlayerCreature())
			return;

	int color = 0; //text color
	String file = "";
	String stringName = "";

	switch (type) {
	case PSG:
		color = 0; //white, unconfirmed
		file = "cbt_spam";
		stringName = "shield_damaged";
		break;
	case FORCESHIELD:
		color = 0; //white, unconfirmed
		file = "cbt_spam";
		stringName = "forceshield_hit";
		item = NULL;
		break;
	case FORCEFEEDBACK:
		color = 0; //white, unconfirmed
		file = "cbt_spam";
		stringName = "forcefeedback_hit";
		item = NULL;
		break;
	case FORCEABSORB:
		color = 0; //white, unconfirmed
		file = "cbt_spam";
		stringName = "forceabsorb_hit";
		item = NULL;
		break;
	case FORCEARMOR:
		color = 0; //white, unconfirmed
		file = "cbt_spam";
		stringName = "forcearmor_hit";
		item = NULL;
		break;
	case ARMOR:
		color = 1; //green, confirmed
		file = "cbt_spam";
		stringName = "armor_damaged";
		break;
	case FOOD:
		color = 0; //white, confirmed
		file = "combat_effects";
		stringName = "mitigate_damage";
		item = NULL;
		break;
	default:
		break;
	}

	CombatSpam* spam = new CombatSpam(defender, NULL, defender, item, damage, file, stringName, color);
	defender->sendMessage(spam);

}

void CombatManager::broadcastCombatSpam(TangibleObject* attacker, TangibleObject* defender, TangibleObject* item, int damage, const String& file, const String& stringName, byte color) {
	if (attacker == NULL)
		return;

	Zone* zone = attacker->getZone();
	if (zone == NULL)
		return;

	CloseObjectsVector* vec = (CloseObjectsVector*) attacker->getCloseObjects();
	SortedVector<QuadTreeEntry*> closeObjects;

	if (vec != NULL) {
		closeObjects.removeAll(vec->size(), 10);
		vec->safeCopyTo(closeObjects);
	} else {
		info("Null closeobjects vector in CombatManager::broadcastCombatSpam", true);
		zone->getInRangeObjects(attacker->getWorldPositionX(), attacker->getWorldPositionY(), COMBAT_SPAM_RANGE, &closeObjects, true);
	}

	for (int i = 0; i < closeObjects.size(); ++i) {
		SceneObject* object = cast<SceneObject*>( closeObjects.get(i));

		if (object->isPlayerCreature() && attacker->isInRange(object, COMBAT_SPAM_RANGE)) {
			CreatureObject* receiver = cast<CreatureObject*>( object);
			CombatSpam* spam = new CombatSpam(attacker, defender, receiver, item, damage, file, stringName, color);
			receiver->sendMessage(spam);
		}
	}
}

void CombatManager::broadcastCombatAction(CreatureObject * attacker, TangibleObject * defenderObject, WeaponObject* weapon, const CreatureAttackData & data, uint8 hit) {
	CombatAction* combatAction = NULL;

	uint32 animationCRC = data.getAnimationCRC();

	if (!attacker->isCreature() && animationCRC == 0)
		animationCRC = getDefaultAttackAnimation(attacker);

	// TODO: this needs to be fixed.
	if (attacker->isCreature() && animationCRC == 0) {
		if (attacker->getGameObjectType() == SceneObjectType::DROIDCREATURE || attacker->getGameObjectType() == SceneObjectType::PROBOTCREATURE)
			animationCRC = STRING_HASHCODE("droid_attack_light");
		else if (weapon->isRangedWeapon())
			animationCRC = STRING_HASHCODE("creature_attack_ranged_light");
		else
			animationCRC = STRING_HASHCODE("creature_attack_light");
	}

	if (defenderObject->isCreatureObject())
		combatAction = new CombatAction(attacker, defenderObject->asCreatureObject(), animationCRC, hit, data.getTrails(), weapon->getObjectID());
	else
		combatAction = new CombatAction(attacker, defenderObject, animationCRC, hit, data.getTrails(), weapon->getObjectID());

	attacker->broadcastMessage(combatAction, true);

	String effect = data.getCommand()->getEffectString();

	if (!effect.isEmpty())
		attacker->playEffect(effect);
}

void CombatManager::requestDuel(CreatureObject* player, CreatureObject* targetPlayer) {
	/* Pre: player != targetPlayer and not NULL; player is locked
	 * Post: player requests duel to targetPlayer
	 */

	Locker clocker(targetPlayer, player);

	PlayerObject* ghost = player->getPlayerObject();
	PlayerObject* targetGhost = targetPlayer->getPlayerObject();

	if (ghost->requestedDuelTo(targetPlayer)) {
		StringIdChatParameter stringId("duel", "already_challenged");
		stringId.setTT(targetPlayer->getObjectID());
		player->sendSystemMessage(stringId);

		return;
	}

	player->info("requesting duel");

	ghost->addToDuelList(targetPlayer);

	if (targetGhost->requestedDuelTo(player)) {
		BaseMessage* pvpstat = new UpdatePVPStatusMessage(targetPlayer,
				targetPlayer->getPvpStatusBitmask()
				| CreatureFlag::ATTACKABLE
				| CreatureFlag::AGGRESSIVE);
		player->sendMessage(pvpstat);

		for (int i = 0; i < targetGhost->getActivePetsSize(); i++) {
			ManagedReference<AiAgent*> pet = targetGhost->getActivePet(i);

			if (pet != NULL) {
				BaseMessage* petpvpstat = new UpdatePVPStatusMessage(pet,
						pet->getPvpStatusBitmask()
						| CreatureFlag::ATTACKABLE
						| CreatureFlag::AGGRESSIVE);
				player->sendMessage(petpvpstat);
			}
		}

		StringIdChatParameter stringId("duel", "accept_self");
		stringId.setTT(targetPlayer->getObjectID());
		player->sendSystemMessage(stringId);

		BaseMessage* pvpstat2 = new UpdatePVPStatusMessage(player,
				player->getPvpStatusBitmask() | CreatureFlag::ATTACKABLE
				| CreatureFlag::AGGRESSIVE);
		targetPlayer->sendMessage(pvpstat2);

		for (int i = 0; i < ghost->getActivePetsSize(); i++) {
			ManagedReference<AiAgent*> pet = ghost->getActivePet(i);

			if (pet != NULL) {
				BaseMessage* petpvpstat = new UpdatePVPStatusMessage(pet,
						pet->getPvpStatusBitmask()
						| CreatureFlag::ATTACKABLE
						| CreatureFlag::AGGRESSIVE);
				targetPlayer->sendMessage(petpvpstat);
			}
		}

		StringIdChatParameter stringId2("duel", "accept_target");
		stringId2.setTT(player->getObjectID());
		targetPlayer->sendSystemMessage(stringId2);
	} else {
		StringIdChatParameter stringId3("duel", "challenge_self");
		stringId3.setTT(targetPlayer->getObjectID());
		player->sendSystemMessage(stringId3);

		StringIdChatParameter stringId4("duel", "challenge_target");
		stringId4.setTT(player->getObjectID());
		targetPlayer->sendSystemMessage(stringId4);
	}
}

void CombatManager::requestEndDuel(CreatureObject* player, CreatureObject* targetPlayer) {
	/* Pre: player != targetPlayer and not NULL; player is locked
	 * Post: player requested to end the duel with targetPlayer
	 */

	Locker clocker(targetPlayer, player);

	PlayerObject* ghost = player->getPlayerObject();
	PlayerObject* targetGhost = targetPlayer->getPlayerObject();

	if (!ghost->requestedDuelTo(targetPlayer)) {
		StringIdChatParameter stringId("duel", "not_dueling");
		stringId.setTT(targetPlayer->getObjectID());
		player->sendSystemMessage(stringId);

		return;
	}

	player->info("ending duel");

	ghost->removeFromDuelList(targetPlayer);
	player->removeDefender(targetPlayer);

	if (targetGhost->requestedDuelTo(player)) {
		targetGhost->removeFromDuelList(player);
		targetPlayer->removeDefender(player);

		player->sendPvpStatusTo(targetPlayer);

		for (int i = 0; i < ghost->getActivePetsSize(); i++) {
			ManagedReference<AiAgent*> pet = ghost->getActivePet(i);

			if (pet != NULL) {
				targetPlayer->removeDefender(pet);
				pet->sendPvpStatusTo(targetPlayer);

				ManagedReference<CreatureObject*> target = targetPlayer;

				EXECUTE_TASK_2(pet, target, {
					Locker locker(pet_p);

					pet_p->removeDefender(target_p);
				});
			}
		}

		StringIdChatParameter stringId("duel", "end_self");
		stringId.setTT(targetPlayer->getObjectID());
		player->sendSystemMessage(stringId);

		targetPlayer->sendPvpStatusTo(player);

		for (int i = 0; i < targetGhost->getActivePetsSize(); i++) {
			ManagedReference<AiAgent*> pet = targetGhost->getActivePet(i);

			if (pet != NULL) {
				player->removeDefender(pet);
				pet->sendPvpStatusTo(player);

				ManagedReference<CreatureObject*> play = player;

				EXECUTE_TASK_2(pet, play, {
					Locker locker(pet_p);

					pet_p->removeDefender(play_p);
				});
			}
		}

		StringIdChatParameter stringId2("duel", "end_target");
		stringId2.setTT(player->getObjectID());
		targetPlayer->sendSystemMessage(stringId2);
	}
}

void CombatManager::freeDuelList(CreatureObject* player, bool spam) {
	/* Pre: player not NULL and is locked
	 * Post: player removed and warned all of the objects from its duel list
	 */
	PlayerObject* ghost = player->getPlayerObject();

	if (ghost == NULL || ghost->isDuelListEmpty())
		return;

	player->info("freeing duel list");

	while (ghost->getDuelListSize() != 0) {
		ManagedReference<CreatureObject*> targetPlayer = ghost->getDuelListObject(0);
		PlayerObject* targetGhost = targetPlayer->getPlayerObject();

		if (targetPlayer != NULL && targetGhost != NULL && targetPlayer.get() != player) {
			try {
				Locker clocker(targetPlayer, player);

				ghost->removeFromDuelList(targetPlayer);
				player->removeDefender(targetPlayer);

				if (targetGhost->requestedDuelTo(player)) {
					targetGhost->removeFromDuelList(player);
					targetPlayer->removeDefender(player);

					player->sendPvpStatusTo(targetPlayer);

					for (int i = 0; i < ghost->getActivePetsSize(); i++) {
						ManagedReference<AiAgent*> pet = ghost->getActivePet(i);

						if (pet != NULL) {
							targetPlayer->removeDefender(pet);
							pet->sendPvpStatusTo(targetPlayer);

							EXECUTE_TASK_2(pet, targetPlayer, {
								Locker locker(pet_p);

								pet_p->removeDefender(targetPlayer_p);
							});
						}
					}

					if (spam) {
						StringIdChatParameter stringId("duel", "end_self");
						stringId.setTT(targetPlayer->getObjectID());
						player->sendSystemMessage(stringId);
					}

					targetPlayer->sendPvpStatusTo(player);

					for (int i = 0; i < targetGhost->getActivePetsSize(); i++) {
						ManagedReference<AiAgent*> pet = targetGhost->getActivePet(i);

						if (pet != NULL) {
							player->removeDefender(pet);
							pet->sendPvpStatusTo(player);

							ManagedReference<CreatureObject*> play = player;

							EXECUTE_TASK_2(pet, play, {
								Locker locker(pet_p);

								pet_p->removeDefender(play_p);
							});
						}
					}

					if (spam) {
						StringIdChatParameter stringId2("duel", "end_target");
						stringId2.setTT(player->getObjectID());
						targetPlayer->sendSystemMessage(stringId2);
					}
				}


			} catch (ObjectNotDeployedException& e) {
				ghost->removeFromDuelList(targetPlayer);

				System::out << "Exception on CombatManager::freeDuelList()\n"
						<< e.getMessage() << "\n";
			}
		}
	}
}

void CombatManager::declineDuel(CreatureObject* player, CreatureObject* targetPlayer) {
	/* Pre: player != targetPlayer and not NULL; player is locked
	 * Post: player declined Duel to targetPlayer
	 */

	Locker clocker(targetPlayer, player);

	PlayerObject* ghost = player->getPlayerObject();
	PlayerObject* targetGhost = targetPlayer->getPlayerObject();

	if (targetGhost->requestedDuelTo(player)) {
		targetGhost->removeFromDuelList(player);

		StringIdChatParameter stringId("duel", "cancel_self");
		stringId.setTT(targetPlayer->getObjectID());
		player->sendSystemMessage(stringId);

		StringIdChatParameter stringId2("duel", "cancel_target");
		stringId2.setTT(player->getObjectID());
		targetPlayer->sendSystemMessage(stringId2);
	}
}

bool CombatManager::areInDuel(CreatureObject* player1, CreatureObject* player2) {
	PlayerObject* ghost1 = player1->getPlayerObject().get();
	PlayerObject* ghost2 = player2->getPlayerObject().get();

	if (ghost1 != NULL && ghost2 != NULL) {
		if (ghost1->requestedDuelTo(player2) && ghost2->requestedDuelTo(player1))
			return true;
	}

	return false;
}

bool CombatManager::checkConeAngle(SceneObject* target, float angle,
		float creatureVectorX, float creatureVectorY, float directionVectorX,
		float directionVectorY) {
	float Target1 = target->getPositionX() - creatureVectorX;
	float Target2 = target->getPositionY() - creatureVectorY;

	float resAngle = atan2(Target2, Target1) - atan2(directionVectorY, directionVectorX);
	float degrees = resAngle * 180 / M_PI;

	float coneAngle = angle / 2;

	if (degrees > coneAngle || degrees < -coneAngle) {
		return false;
	}

	return true;
}

uint32 CombatManager::getDefaultAttackAnimation(CreatureObject* creature) {
	WeaponObject* weapon = creature->getWeapon();

	if (weapon->isRangedWeapon())
		return 0x506E9D4C;
	else
		return defaultAttacks[System::random(8)];
}

int CombatManager::doAreaCombatAction(CreatureObject* attacker, WeaponObject* weapon, TangibleObject* defenderObject, const CreatureAttackData& data) {
	float creatureVectorX = attacker->getPositionX();
	float creatureVectorY = attacker->getPositionY();

	float directionVectorX = defenderObject->getPositionX() - creatureVectorX;
	float directionVectorY = defenderObject->getPositionY() - creatureVectorY;

	Zone* zone = attacker->getZone();

	if (zone == NULL)
		return 0;

	PlayerManager* playerManager = zone->getZoneServer()->getPlayerManager();

	int damage = 0;

	int range = data.getAreaRange();

	if (data.getCommand()->isConeAction()) {
		range = data.getRange();
	}

	if (range < 0) {
		range = weapon->getMaxRange();
	}

	if (weapon->isThrownWeapon() || weapon->isHeavyWeapon())
		range = weapon->getMaxRange() + data.getAreaRange();

	try {
		//zone->rlock();

		CloseObjectsVector* vec = (CloseObjectsVector*) attacker->getCloseObjects();

		SortedVector<QuadTreeEntry*> closeObjects;

		if (vec != NULL) {
			closeObjects.removeAll(vec->size(), 10);
			vec->safeCopyTo(closeObjects);
		} else {
			attacker->info("Null closeobjects vector in CombatManager::doAreaCombatAction", true);
			zone->getInRangeObjects(attacker->getWorldPositionX(), attacker->getWorldPositionY(), 128, &closeObjects, true);
		}

		for (int i = 0; i < closeObjects.size(); ++i) {
			SceneObject* object = cast<SceneObject*>(closeObjects.get(i));

			TangibleObject* tano = object->asTangibleObject();

			if (tano == NULL) {
				continue;
			}

			if (object == attacker) {
				//error("object is attacker");
				continue;
			}

			if (!tano->isAttackableBy(attacker)) {
				//error("object is not attackable");
				continue;
			}

			if (!attacker->isInRange(object, range + object->getTemplateRadius() + attacker->getTemplateRadius())) {
				//error("not in range " + String::valueOf(range));
				continue;
			}

			if (weapon->isThrownWeapon() || weapon->isHeavyWeapon()) {
				if (!(tano == defenderObject) && !(tano->isInRange(defenderObject, data.getAreaRange() + defenderObject->getTemplateRadius())))
					continue;
			}

			if (tano->isCreatureObject() && tano->asCreatureObject()->isIncapacitated()) {
				//error("object is incapacitated");
				continue;
			}

			if (data.getCommand()->isConeAction() && !checkConeAngle(tano, data.getConeAngle(), creatureVectorX, creatureVectorY, directionVectorX, directionVectorY)) {
				//error("object is not in cone angle");
				continue;
			}

			//			zone->runlock();

			try {
				if (tano == defenderObject || (!(weapon->isThrownWeapon()) && !(weapon->isHeavyWeapon()))) {
					if (CollisionManager::checkLineOfSight(object, attacker)) {
						damage += doTargetCombatAction(attacker, weapon, tano, data);
					}
				} else {
					if (CollisionManager::checkLineOfSight(object, defenderObject)) {
						damage += doTargetCombatAction(attacker, weapon, tano, data);
					}
				}
			} catch (Exception& e) {
				error(e.getMessage());
			} catch (...) {
				//zone->rlock();

				throw;
			}

			//			zone->rlock();
		}

		//		zone->runlock();
	} catch (...) {
		//		zone->runlock();

		throw;
	}

	if (data.getAttackType() == CombatManager::WEAPONATTACK)
		weapon->decreasePowerupUses(attacker);

	return damage;
}

int CombatManager::doAreaCombatAction(TangibleObject* attacker, WeaponObject* weapon, TangibleObject* defenderObject, const CreatureAttackData& data){
	float creatureVectorX = attacker->getPositionX();
	float creatureVectorY = attacker->getPositionY();

	float directionVectorX = defenderObject->getPositionX() - creatureVectorX;
	float directionVectorY = defenderObject->getPositionY() - creatureVectorY;

	Zone* zone = attacker->getZone();

	if (zone == NULL)
		return 0;

	PlayerManager* playerManager = zone->getZoneServer()->getPlayerManager();

	int damage = 0;

	int range = data.getAreaRange();

	if (data.getCommand()->isConeAction()) {
		range = data.getRange();
	}

	if (range < 0) {
		range = weapon->getMaxRange();
	}

	try {
		//zone->rlock();

		CloseObjectsVector* vec = (CloseObjectsVector*) attacker->getCloseObjects();

		SortedVector<QuadTreeEntry*> closeObjects;

		if (vec != NULL) {
			closeObjects.removeAll(vec->size(), 10);
			vec->safeCopyTo(closeObjects);
		} else {
			attacker->info("Null closeobjects vector in CombatManager::doAreaCombatAction", true);
			zone->getInRangeObjects(attacker->getWorldPositionX(), attacker->getWorldPositionY(), 128, &closeObjects, true);
		}

		for (int i = 0; i < closeObjects.size(); ++i) {
			SceneObject* object = cast<SceneObject*>(closeObjects.get(i));

			TangibleObject* tano = object->asTangibleObject();

			if (tano == NULL) {
				continue;
			}

			if (object == attacker) {
				//error("object is attacker");
				continue;
			}

			if (!(tano->getPvpStatusBitmask() & CreatureFlag::ATTACKABLE)) {
				//error("object is not attackable");
				continue;
			}

			if (!attacker->isInRange(object, range + object->getTemplateRadius() + attacker->getTemplateRadius())) {
				//error("not in range " + String::valueOf(range));
				continue;
			}

			if (tano->isCreatureObject() && tano->asCreatureObject()->isIncapacitated()) {
				//error("object is incapacitated");
				continue;
			}

			if (data.getCommand()->isConeAction() && !checkConeAngle(tano, data.getConeAngle(), creatureVectorX, creatureVectorY, directionVectorX, directionVectorY)) {
				//error("object is not in cone angle");
				continue;
			}

			//			zone->runlock();

			try {
				if (CollisionManager::checkLineOfSight(object, attacker)) {
					damage += doTargetCombatAction(attacker, weapon, tano, data);

				}
			} catch (Exception& e) {
				error(e.getMessage());
			} catch (...) {
				//zone->rlock();

				throw;
			}

			//			zone->rlock();
		}

		//		zone->runlock();
	} catch (...) {
		//		zone->runlock();

		throw;
	}

	return damage;
	return 0;
}

int CombatManager::getArmorTurretReduction(CreatureObject* attacker, TangibleObject* defender, int damageType) {
	int resist = 0;

	if (defender != NULL && defender->isTurret()) {
		DataObjectComponentReference* data = defender->getDataObjectComponent();

		if (data != NULL) {

			TurretDataComponent* turretData = cast<TurretDataComponent*>(data->get());

			if (turretData != NULL) {

				switch (damageType) {
				case WeaponObject::KINETIC:
					resist = turretData->getKinetic();
					break;
				case WeaponObject::ENERGY:
					resist = turretData->getEnergy();
					break;
				case WeaponObject::ELECTRICITY:
					resist = turretData->getElectricity();
					break;
				case WeaponObject::STUN:
					resist = turretData->getStun();
					break;
				case WeaponObject::BLAST:
					resist = turretData->getBlast();
					break;
				case WeaponObject::HEAT:
					resist = turretData->getHeat();
					break;
				case WeaponObject::COLD:
					resist = turretData->getCold();
					break;
				case WeaponObject::ACID:
					resist = turretData->getAcid();
					break;
				case WeaponObject::LIGHTSABER:
					resist = turretData->getLightSaber();
					break;
				}
			}
		}
	}

	return resist;
}
