#include <random>
#include <time.h>

#include "bnHitBox.h"
#include "bnTile.h"
#include "bnField.h"
#include "bnTextureResourceManager.h"
#include "bnAudioResourceManager.h"

HitBox::HitBox(Field* _field, Team _team, int _damage) : Spell() {
  field = _field;
  team = _team;
  direction = Direction::NONE;
  deleted = false;
  hit = false;
  srand((unsigned int)time(nullptr));
  cooldown = 0;
  damage = _damage;

  EnableTileHighlight(false);
}

HitBox::~HitBox(void) {
}

void HitBox::Update(float _elapsed) {
  tile->AffectEntities(this);
  this->Delete();
}

bool HitBox::Move(Direction _direction) {
  return false;
}

void HitBox::Attack(Character* _entity) {

  if (_entity && _entity->GetTeam() != this->GetTeam()) {
    _entity->Hit(damage);
  }
}
