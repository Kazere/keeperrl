/* Copyright (C) 2013-2014 Michal Brzozowski (rusolis@poczta.fm)

   This file is part of KeeperRL.

   KeeperRL is free software; you can redistribute it and/or modify it under the terms of the
   GNU General Public License as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   KeeperRL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
   even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along with this program.
   If not, see http://www.gnu.org/licenses/ . */

#pragma once

#include <string>

#include "singleton.h"
#include "enums.h"

RICH_ENUM(SkillId,
  AMBUSH,
  KNIFE_THROWING,
  STEALING,
  SWIMMING,
  ARCHERY,
  WEAPON_MELEE,
  UNARMED_MELEE,
  CONSTRUCTION,
  ELF_VISION,
  NIGHT_VISION,
  DISARM_TRAPS,
  SORCERY,
  CONSUMPTION,
  HEALING,
  STEALTH,
  WORKSHOP,
  FORGE,
  LABORATORY,
  JEWELER,
  FURNACE,
  MANA
);

class Creature;
class Skill : public Singleton<Skill, SkillId> {
  public:
  string getName() const;
  string getNameForCreature(const Creature*) const;
  string getHelpText() const;
  bool transferOnConsumption() const;
  bool isDiscrete() const;

  int getModifier(const Creature*, ModifierType) const;

  static void init();

  private:
  string name;
  string helpText;
  bool consume;
  bool discrete;
  Skill(string name, string helpText, bool discrete, bool canConsume = true);
};

class Skillset {
  public:
  void insert(SkillId);
  void erase(SkillId);
  bool hasDiscrete(SkillId) const;
  double getValue(SkillId) const;
  void setValue(SkillId, double);
  const EnumSet<SkillId>& getAllDiscrete() const;

  template <class Archive>
  void serialize(Archive& ar, const unsigned int version);

  private:
  EnumMap<SkillId, double> SERIAL(gradable);
  EnumSet<SkillId> SERIAL(discrete);
};

