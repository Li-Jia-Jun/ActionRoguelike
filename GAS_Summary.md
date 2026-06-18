# Custom GAS Architecture Summary

This project implements a custom Gameplay Ability System (GAS) — not Unreal's built-in GAS. Everything lives under `Source/ActionRoguelike/ActionSystem/`.

---

## Overview

**What is a Gameplay Ability System?** It's the standard architecture in modern action/RPG games for answering three questions: *what can a character do* (abilities), *what numbers describe a character* (attributes like health, mana, damage), and *what changes those numbers over time* (effects like damage, heals, buffs, and debuffs). Rather than scattering this logic across individual character classes — where a fireball would directly reach into a target and subtract health — a GAS centralizes it into one component that every character owns. Each character has an **Ability System Component (ASC)** that holds their attributes and runs their abilities and effects, so all gameplay interactions flow through a single, consistent place.

The core idea is **decoupling through data**. An ability (a fireball) doesn't know anything about its target's internals; it just applies a **Gameplay Effect** (a data-defined "deal 20 fire damage and apply burn"). The effect doesn't know who fired it; it just describes a change to attributes. Attributes don't know what's modifying them; they just recalculate from a base value plus whatever modifiers are currently active. **Gameplay Tags** — hierarchical labels like `Status.Burn` or `State.Casting` — glue these pieces together loosely, letting one system ask "is this character burning?" without holding a hard reference to whatever caused it. The payoff is that designers can author new abilities and effects as data assets, mix and match them, and the engine handles applying, stacking, timing, and cleaning them up uniformly.

Unreal ships with its own production-grade GAS, but it's large and notoriously hard to learn. This project reimplements the same core concepts — ASC, attributes, effects, abilities, tags — as a smaller, self-contained system, both to learn how those pieces fit together and to make deliberate, different design choices along the way (documented below).

![Potions](./docs/images/Potions.png)

## Design Rationale & Tradeoffs

This system was built primarily as a way to learn Unreal and to understand the design decisions inside Epic's GAS — not to claim it improves on it. The most useful part of the exercise was rebuilding the core loop from scratch and feeling where the tradeoffs actually live. The notes below capture the places where this implementation diverges from stock GAS and why, since the reasoning is more interesting than the code itself.

### UDataAsset vs. DataTable for the AttributeSet

Epic's GAS typically drives attribute initialization from a **DataTable** — a row-based, spreadsheet-style structure where every attribute is a row sharing one row struct, referenced through an `FDataTableRowHandle`. That's a good fit when you have hundreds of attributes to bulk-tune like a spreadsheet.

This project instead uses a **`UDataAsset`** (`URogueAttributeSetTemplate`) to define the starting attributes. The motivation was strong typing, direct object references instead of row handles, and proper UObject inheritance — a template can derive from another template and specialize it. The tradeoff is real and worth stating plainly: DataAssets are less convenient for bulk editing, since you're editing individual assets rather than scanning a single table. For a project of this scale, the type safety and inheritance were worth more than spreadsheet-style editing, but the calculus would flip on a project with a very large, frequently-retuned attribute list.

![Player AttributeSet](./docs/images/PlayerAttributeSet.png)

### Unified Tag Map, Separated Ownership

Stock GAS keeps GE-granted tags inside the replicated `FActiveGameplayEffectsContainer` and ability-owned tags as loose tags, but unifies them for queries through a single `FGameplayTagCountContainer`. This implementation follows the same principle deliberately: a single `ActiveTagCountMap` answers every `HasTag`-style query regardless of source, while ownership and cleanup responsibility stay separated per system (`FDebuffHandle` for effects, direct decrement for abilities). Arriving at this split independently and then finding Epic does essentially the same thing was a useful confirmation that the separation of *query surface* from *ownership* is a sound instinct, not an accident of their codebase.

*(Screenshot placeholder: ActiveTagCountMap state during an active burn + cast.)*

### Handle-Based Identity for Debuffs

An earlier version of `FAttributeDebuffData` cached the index of the granting effect instance so the effect could find its own debuff to remove on finish. That was fragile — container reordering or removal could shift indices out from under the reference. The refactor to `FDebuffHandle` decouples *semantic identity* (the tag, e.g. `Status.Burn`, used for queries and UI) from *ownership identity* (a unique handle the granter stores and removes by). This mirrors Epic's `FActiveGameplayEffectHandle`, and it cleanly resolves the case where two different effects grant the same tag with different modifiers. The evolution from cached-index to handle is itself a good illustration of understanding *why* the more robust design is necessary, rather than reaching for it by default.

### Deliberately Out of Scope

Some of stock GAS's complexity was intentionally left out to keep the focus on the core attribute / effect / ability loop:

- **Prediction** — Epic's client-side prediction system is a significant part of why GAS is complex; this implementation is authoritative-only.
- **Full replication** — loose-style tags here don't auto-replicate the way Epic's replicated effect container does. Replicating the active-effect/tag state the way `FActiveGameplayEffectsContainer` does would be the natural next step, and it's also where most of the genuine difficulty lives.

Scoping these out was a choice to understand the fundamentals first, with a clear sense of what the harder, networked version would require.


## Components at a Glance

| Component | File | Role |
|---|---|---|
| `URogueActionSystemComponent` | `ActionSystem/RogueActionSystemComponent` | Central hub: owns attributes, manages abilities & effects |
| `URogueAttributeSet` | `ActionSystem/AttributeSet/RogueAttributeSet` | Stores and recalculates all character stats |
| `URogueGameplayAbility` | `ActionSystem/GameplayAbility/RogueGameplayAbility` | Base class for all abilities |
| `URogueGameplayEffect` | `ActionSystem/GameplayEffect/RogueGameplayEffect` | Data asset defining a single effect |
| `URogueGameplayEffectInstance` | `ActionSystem/GameplayEffect/RogueGameplayEffectInstance` | Runtime instance of an active effect |
| Duration Policy Instances | `ActionSystem/GameplayEffect/RogueGameplayEffectDurationPolicy` | Manage timing (instant / timed / periodic) |

---


## 1. Attribute System

`URogueAttributeSet` stores attributes in three layers:

- **`FAttributeNumericData[]`** — each attribute has a `Tag`, `BaseValue` (permanent), and `CurrentValue` (after modifiers).
- **`FAttributeDebuffData[]`** — temporary conditions applied by effects. Each debuff has:
  - `Tag` — semantic identity of the condition (e.g. `Status.Burn`), used for querying; owned by the debuff itself, not cached from the granting GE
  - `Handle` (`FDebuffHandle`) — unique ownership identity; the granting GE stores this handle and removes by it, so multiple sources can grant the same tag independently without ambiguity
  - `Modifiers[]` — the actual stat changes applied to `CurrentValue`
- **`FRogueAttributeRelationship[]`** — constraints like `Health <= MaxHealth`, enforced after every recalculation.

Recalculation order every time anything changes:
1. Reset `CurrentValue = BaseValue` for all attributes
2. Apply all active debuff modifiers to `CurrentValue`
3. Clamp via relationship constraints

The initial values come from a `URogueAttributeSetTemplate` data asset assigned on the ASC.

`URogueAttributeSet` should only be changed by ASC for event broadcasting.

---

## 2. Gameplay Effects

Effects are **data assets** (`URogueGameplayEffect`) with three orthogonal policies configured via `InstancedStruct`:

### Modify Policy — *what* changes
| Policy | Effect |
|---|---|
| `FRogueGameplayEffectPermanentModify` | Changes `BaseValue` permanently; can also cure specific debuffs |
| `FRogueGameplayEffectDebuffModify` | Pushes debuffs into `AttributeSet.Debuffs[]`, affecting only `CurrentValue` |

### Duration Policy — *how long*
| Policy | Behavior |
|---|---|
| `FRogueGameplayEffectInstantApply` | Fires once, no instance created |
| `FRogueGameplayEffectDurationApply` | Fires once after `Duration` seconds (-1 = infinite until terminated) |
| `FRogueGameplayEffectPeriodicApply` | Fires every `Interval` seconds for `TotalCount` times (-1 = infinite) |

### Stack Policy — *how many*
- `eIndependent` — multiple instances coexist
- `eRefresh` — new application replaces old
- `eAccumulate` — stacks up to `StackLimit`

Gate conditions: `ImmunityEffects[]` (blocked if target has any) and `PreconditionEffects[]` (blocked if target lacks any).

### Effect Application Flow

```
ASC.ApplyGameplayEffectToSelf(Effect)
  → CanApplyGameplayEffect()          // immunity, precondition, stack checks
  → Create URogueGameplayEffectInstance
  → Instance.Init() + Instance.Start()
      → Duration policy instance created, timer set up
  → Instance.Apply() fires:
      PermanentModify → AttributeSet.ApplyModifiers()  (BaseValue changed)
      DebuffModify    → AttributeSet.ApplyDebuffs()     (debuff pushed)
  → AttributeSet.RecalculateAttributes()
  → ASC.AttributeSetChangedDelegate broadcast → UI updates
```

When the duration expires (or `Terminate()` is called for infinite effects):
```
Instance.Finish()
  → AttributeSet.RemoveDebuffs()       // only for debuff-type effects
  → AttributeSet.RecalculateAttributes()
  → ASC.AttributeSetChangedDelegate broadcast
  → Instance.OnFinishedDelegate → ASC removes instance from ActiveEffects
```

---

## 3. Tag System

The ASC owns a single unified tag count map:

```cpp
TMap<FGameplayTag, int32> ActiveTagCountMap;
```

A tag is considered "active" when its count > 0. Two systems write to it via different paths:

| Source | Tags | Example | Lifetime |
|---|---|---|---|
| GE `GrantedTags` | Semantic state (debuff tags) | `Status.Burn`, `Status.Vulnerable` | Lives with the GE instance; removed via `FDebuffHandle` in `Finish()` |
| GA `ActivationOwnedTags` | Execution state (pure tags) | `State.Casting`, `State.Attacking` | Lives with ability activation; removed in `EndAbility()` |

Both paths call the same increment/decrement on `ActiveTagCountMap`, so all tag queries (`HasTag`, `HasAnyTag`) are unified regardless of source.

### FDebuffHandle

Each `FAttributeDebuffData` is assigned a `FDebuffHandle` (int32 wrapped in a struct) when granted. The granting GE stores this handle and uses it for unambiguous removal — tag alone is insufficient because two GEs can grant the same `Status.Burn` with different modifiers:

```
GE_Fireball  → Handle(42), Status.Burn, -5 HP/sec
GE_Firesword → Handle(43), Status.Burn, -3 HP/sec + slow

GE_Fireball.Finish() → RemoveDebuffByHandle(42)  // only removes Fireball's burn
```

---

## 4. Gameplay Abilities

`URogueGameplayAbility` is a blueprintable base class. Each ability specifies:
- `CostEffect` — must be instant apply; consumed at `CommitAbility()`
- `CooldownEffect` — must be duration apply; applied at `CommitAbility()`
- `ActiveEffects[]` — must be debuff-type infinite duration; applied at commit, removed at `EndAbility()`

### Ability Lifecycle

```
TryActivateAbilityByTag(Tag)
  → Find ability in ASC.GrantedAbilitySpecs
  → CanActivateAbility()
      - CanAffordModifiers(CostEffect)
      - Cooldown not active
      - ActiveEffects can be applied
  → ActivateAbility()           // bIsActivated = true
  → ... custom logic ...
  → CommitAbility()
      - Apply CostEffect (instant → BaseValue change)
      - Apply CooldownEffect (timed effect, blocks re-activation)
      - Apply ActiveEffects (infinite debuffs while ability runs)
  → ... more custom logic (spawn projectile, play VFX, etc.) ...
  → EndAbility()
      - Terminate all ActiveEffect instances
      - bIsActivated = false
      - Broadcast AbilityEndedDelegate → ASC cleans up
```

### Instance Policy

| Policy | Behavior |
|---|---|
| `eNotInstanced` | Uses CDO; not tracked in `ActiveAbilities` |
| `eInstancePerActor` | Single instance created at grant time, reused |
| `eInstancePerExecution` | Duplicate created each activation |

### Example: `UAbility_CastProjectile`

1. `ActivateAbility` — play cast montage, spawn hand VFX, play sound
2. Montage fires event tag → `OnAnimMontageEventReceived` callback
3. `CommitAbility` — deduct mana cost, start cooldown, apply casting debuffs
4. Trace from camera to aim point → spawn `ProjectileClass` at socket
5. Montage finishes → `EndAbility`

---

## 5. Modifier System

`FRogueGameplayEffectModifier` describes a single stat change:

| Type | Description |
|---|---|
| `eFlatValue` | Add/subtract/multiply by a constant (`FlatValue`) |
| `eOtherAttributeMagnitude` | Scale from another attribute's `CurrentValue × Magnitude` |

`CanAffordModifiers()` tests the change against `CurrentValue` without committing — used for cost checks before activating an ability.

---

## 6. Gameplay Events

`FRogueGameplayEventData` carries event payloads:
- `EventTag` — identifies the event type (e.g., the anim montage notify tag)
- `SourceObject` / `TargetObject` — participants
- `Value` — numeric payload (damage, heal amount, etc.)

`ASC.HandleGameplayEvent()` broadcasts to `GameplayEventReceivedDelegate`. Abilities bind to this to react to animation notifies or hit events without polling.

---

## 7. Character Setup

### Player (`ASPlayerCharacter`)
- Creates ASC in constructor
- Assigns `AttributeSetTemplate` in editor
- `BeginPlay`: grants all player abilities (primary, secondary, sprint, etc.)
- Enhanced Input actions → `TryActivateAbilityByTag()`
- Subscribes to `AttributeSetChangedDelegate` to scale movement speed from an attribute

### AI (`ARogueAICharacter`)
- Creates ASC in constructor
- Attaches `URogueAttributeBarWidgetComponent` for floating health bar
- Behavior Tree drives ability activation via `URogueBTTask_ApplyGameplayEffect`

---

## 8. UI

### `URogueAttributeBarWidgetComponent`
- Attached to actor; configured with `CurrentAttributeTag` and `MaxAttributeTag`
- On `BeginPlay`, finds ASC on owner and calls `Widget.InitializeWithASC()`

### `URogueAttributeBarWidget`
- Subscribes to `ASC.AttributeSetChangedDelegateCPP`
- On each change: `percent = CurrentAttribute / MaxAttribute` → update progress bar

---

## 9. Full Dataflow (Player Ability)

```
[Input Action]
      ↓
ASPlayerCharacter.TryActivateAbilityByTag(Tag)
      ↓
URogueActionSystemComponent
  → Find spec in GrantedAbilitySpecs
  → CanActivateAbility(): cost afford + cooldown + active effects
      ↓
URogueGameplayAbility.ActivateAbility()
  → PlayAnimMontageAndTrackEvent() ──────────────────────────────┐
      ↓                                                           │
CommitAbility()                                           [Anim Notify fires]
  → ApplyGameplayEffectToSelf(CostEffect)                        │
  → ApplyGameplayEffectToSelf(CooldownEffect)             FRogueGameplayEventData
  → ApplyGameplayEffectToSelf(ActiveEffects[])                   │
      ↓                                                           │
 each effect →                                    OnAnimMontageEventReceived()
  RecalculateAttributes()                                        ↓
  AttributeSetChangedDelegate                         CastProjectile()
      ↓                                               Spawn projectile →
   UI updates                                         Hit → ApplyGameplayEffectToTarget()
                                                             ↓
                                                      RecalculateAttributes()
                                                      AttributeSetChangedDelegate
                                                             ↓
                                                          UI updates
      ↓
OnAnimMontageFinished → EndAbility()
  → Terminate ActiveEffect instances
  → RemoveDebuffs() → RecalculateAttributes() → UI updates
  → AbilityEndedDelegate → ASC removes from ActiveAbilities
```

---

## 10. Key Design Choices

- **Custom GAS, not Unreal's built-in** — full control over attribute math, effect stacking, and instancing.
- **Data-driven via data assets** — `URogueGameplayEffect` and `URogueAttributeSetTemplate` are configured in editor, no hardcoded values.
- **Tag-based identity everywhere** — abilities, effects, attributes, and events all use `FGameplayTag` for loose coupling.
- **InstancedStruct for policies** — modify/duration policies use `InstancedStruct` so new policy types can be added without changing the base effect class.
- **Delegate-driven UI** — widgets never poll; they react to `AttributeSetChangedDelegate` broadcasts.
- **Separation of template vs. instance** — `URogueGameplayEffect` (data) vs. `URogueGameplayEffectInstance` (runtime) keeps config clean and runtime state isolated.
- **Unified tag query, separated ownership** — GE debuff tags and GA execution tags both write to a single `ActiveTagCountMap` on the ASC for uniform querying, but each system tracks ownership separately (`FDebuffHandle` for GE, direct decrement for GA) to guarantee clean cleanup at `Finish()` and `EndAbility()`.
- **Handle-based debuff ownership** — `FDebuffHandle` (unique int32 per grant) decouples tag identity (`Status.Burn` = "what condition") from ownership ("whose burn"), allowing multiple sources to grant the same tag with independent modifiers and unambiguous removal.

---


