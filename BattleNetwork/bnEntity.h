#pragma once
#include <string>
using std::string;

#include "bnAnimation.h"
#include "bnDirection.h"
#include "bnTeam.h"
#include "bnMemory.h"
#include "bnEngine.h"
#include "bnTextureType.h"
#include "bnElements.h"

namespace Battle {
  class Tile;
}

class Field;

class Entity : public LayeredDrawable {
  friend class Field;

public:
  Entity();
  virtual ~Entity();

  virtual const bool Hit(int damage);
  virtual const float GetHitHeight() const;
  virtual void Update(float _elapsed);
  virtual bool Move(Direction _direction);
  virtual bool CanMoveTo(Battle::Tile* next);
  virtual vector<Drawable*> GetMiscComponents();
  virtual TextureType GetTextureType();

  bool Teammate(Team _team);

  void SetTile(Battle::Tile* _tile);
  Battle::Tile* GetTile() const;
  void SlideToTile(bool );

  void SetField(Field* _field);

  Field* GetField() const;

  Team GetTeam() const;
  void SetTeam(Team _team);

  void SetPassthrough(bool state);
  bool IsPassthrough();

  void SetFloatShoe(bool state);
  bool HasFloatShoe();
  Direction GetPreviousDirection();

  void Delete();
  bool IsDeleted() const;

  void SetElement(Element _elem);
  const Element GetElement() const;

  void AdoptNextTile();

protected:
  bool ownedByField;
  Battle::Tile* next;
  Battle::Tile* tile;
  Battle::Tile* previous;
  sf::Vector2f tileOffset;
  sf::Vector2f slideStartPosition;
  Field* field;
  Team team;
  Element element;
  bool passthrough;
  bool floatShoe;
  bool isSliding; // If sliding/gliding to a tile
  bool deleted;
  int moveCount;
  sf::Time slideTime; // how long slide behavior lasts
  double elapsedSlideTime; // in seconds
  Direction direction;
  Direction previousDirection;
};