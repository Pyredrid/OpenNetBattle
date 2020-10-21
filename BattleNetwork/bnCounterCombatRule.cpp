#include "bnCounterCombatRule.h"
#include "battlescene/bnBattleSceneBase.h"

CounterCombatRule::CounterCombatRule(BattleSceneBase* battleScene) 
  : DefenseRule(Priority(0), DefenseOrder::collisionOnly), battleScene(battleScene) {
}

CounterCombatRule::~CounterCombatRule() { }

void CounterCombatRule::CanBlock(DefenseFrameStateJudge& judge, Spell& in, Character& owner) {
  // we lose counter ability if hit by an impact attack
  if ((in.GetHitboxProperties().flags & Hit::impact) == Hit::impact) {
    battleScene->HandleCounterLoss(owner); // see if battle scene has blessed this character with an ability
  }
}