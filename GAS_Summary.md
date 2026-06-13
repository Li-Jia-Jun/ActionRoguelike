# Custom GAS Architecture Summary

This project implements a custom Gameplay Ability System (GAS) — not Unreal's built-in GAS. Everything lives under `Source/ActionRoguelike/ActionSystem/`.

---

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
