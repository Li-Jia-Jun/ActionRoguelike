# TODO

## GAS

- [ ] Implement `ActivationOwnedTags` in GA — pure execution-state tags (e.g. `State.Casting`) added to ASC's `ActiveTagCountMap` on activation, removed on `EndAbility()`
- [ ] Implement `GrantedTags` in GE — each GE grants an `FAttributeDebuffData` whose semantic `Tag` (e.g. `Status.Burn`) is written into `ActiveTagCountMap`; ownership tracked via `FDebuffHandle` so multiple sources can grant the same tag independently
- [ ] Ensure tag cleanup is always guaranteed at the single exit point of each system:
  - GE: all termination paths (expiry, force-remove, stack depletion, owner death) must funnel through `Finish()` — calls `RemoveDebuffByHandle()`, which decrements `ActiveTagCountMap`
  - GA: all termination paths (cancel, interrupt, owner death) must funnel through `EndAbility()` — decrements `ActiveTagCountMap` for each `ActivationOwnedTag`
  - If any path bypasses these, tag counts will leak and tags will persist on the ASC indefinitely
