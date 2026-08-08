# `cs2.weapons`

`cs2.weapons` manages a player's live inventory and exposes weapon state, magazine values, reserve values, and the player's ammo array.

```lua
local weapons = require("cs2.weapons")
```

## Weapon objects

A weapon table contains:

```text
valid
entity_index
handle
owner_slot
active
classname
slot
clip1
clip2
reserve1
reserve2
```

`weapon.slot` is LuaCS's semantic classification:

```text
primary
secondary
knife
grenade
equipment
c4
melee
other
```

`other` is a fallback classification and cannot be passed as a replacement slot.

## Inventory lookup

```text
weapons.list(player)
weapons.count(player)
weapons.get(entity_index_or_weapon)
weapons.active(player)
weapons.find(player, classname)
weapons.has(player, classname)
weapons.refresh(weapon_or_entity_index)
```

`find` compares classnames case-insensitively and returns one weapon or `nil`.

## Give

```lua
local weapon, err = weapons.give(player, "weapon_ak47")
```

Use exact CS2 classnames. Whitespace/control characters and invalid names are rejected.

## Remove

```text
weapons.remove(player, weapon [, delete_entity=true])
weapons.remove_by_classname(player, classname [, delete_entity=true])
weapons.remove_all(player)
```

A weapon object also supports:

```lua
weapon:remove()
```

LuaCS repairs stale inventory handles during removal/enumeration instead of leaving dead entries in `m_hMyWeapons`.

## Drop and switch

```text
weapons.drop(player, weapon)
weapons.drop_active(player)
weapons.switch(player, weapon)
```

Object methods:

```lua
weapon:drop()
weapon:switch()
```

## Replace one semantic slot

```lua
local replacement, err = weapons.replace_slot(
    player,
    "primary",
    "weapon_awp",
    true
)
```

Signature:

```text
weapons.replace_slot(player, slot, classname [, equip=true])
```

Accepted input slots:

```text
auto
primary
secondary
knife
grenade
equipment
c4
melee
```

`auto` classifies the target classname and selects that slot. If you specify a slot explicitly, the classname must belong to that slot.

Replacement removes matching inventory entries in reverse inventory order, gives the new weapon, and optionally equips it. If equipping the new weapon fails, LuaCS removes the newly-created replacement rather than leaving the operation half-complete.

## Magazine and reserve ammo

Read the current values from a refreshed weapon object:

```lua
local weapon = weapons.active(player)
if weapon then
    weapon:refresh()
    print("magazine:", weapon.clip1)
    print("reserve:", weapon.reserve1)
end
```

Write them with:

```text
weapons.set_clip1(weapon, value)
weapons.set_clip2(weapon, value)
weapons.set_reserve1(weapon, value)
weapons.set_reserve2(weapon, value)
```

or methods:

```lua
weapon:set_clip1(30)
weapon:set_reserve1(90)
```

Clip values accept `-1` or a non-negative 32-bit integer. Reserve values must be non-negative.

## Player ammo array

This is separate from a weapon's `clip1`/`reserve1` fields:

```text
weapons.get_ammo(player, ammo_type)
weapons.set_ammo(player, ammo_type, amount)
```

`ammo_type` must be `0..31`. `set_ammo` accepts `0..65535`.

```lua
local old = weapons.get_ammo(player, 0)
weapons.set_ammo(player, 0, old)
```

The example above is a useful non-destructive check that the read/write path works.

## Failure behavior

Inventory mutations normally return `true` or `nil, error_message`. Functions that create or fetch a weapon return the weapon table or `nil, error_message`. Stale/invalid Source 2 weapon state is rejected rather than converted into a partially valid object.