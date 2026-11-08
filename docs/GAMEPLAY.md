# Combat openings and executions

Manual execution prefers an eligible crosshair target, then the best target in front of the player. Targets must be within 250 game units, within 70 degrees of facing, visible, hostile, alive, loaded and supported by an existing race/weapon idle pool. Essential NPCs, children, the player and teammates cannot be victims. Protected NPCs can only be finished by the player. Manual input, notifications, automatic execution and deferred playback share these conditions. The engine decides whether it accepts an idle; Diagnostics reports its result. This update adds no animation assets or graph events.

Normal enemies can be executed after stun breaks **or** at/below the configured health threshold, initially 20%. Elite and Boss enemies require low health even when broken. Their broken state instead grants a damage opening, initially 1.5 times unblocked melee damage. Classification does not bypass quest protection or unsupported animations.

The broken window lasts 3.5 active seconds by default. Additional hits do not extend it. Stun restores at expiry; Elite/Boss enemies then resist stun damage for 5 active seconds. Ordinary incomplete stun recovers outside combat after the existing 3-second delay, at one full bar per 7 seconds. Pausing stops these timers. Fresh light-attack stun defaults to 0.3 times its base, heavy to 1.0, counters and parries to 1.5. A registered light swing earns at most one stamina hit reward, including multiple collision callbacks or targets.

Player automatic execution remains off by default. Follower automatic execution has a separate switch, also off by default. Bind the manual key in **Valhalla Combat → Settings 设置 → 眩晕 / 处决**. The opportunity notification appears only for eligible manual targets. The menu shows the crosshair target's class, matching rule and eligibility reason, or the selected eligible target when no actor is under the crosshair. These diagnostics currently use English names.

## Enemy classification

Recognition priority:

1. Actor reference override in `[References]`.
2. NPC base override in `[NPCs]`, then the original leveled NPC base if available.
3. Configured keyword in `[Keywords]`. Multiple matches choose Boss, then Elite, then Normal.
4. The actor's location or a parent location marks that exact reference with Skyrim's default Boss location reference type.
5. Normal when no rule matches.

Explicit Normal overrides lower priority Boss recognition. There is no level, health, name or race heuristic. Elite is configuration only. Unmarked custom bosses need a patch; a TrueHUD boss bar alone does not classify an actor here.

Place `.ini` files under `Data/SKSE/Plugins/ValhallaCombat/EnemyClassification/`. The included `00-Example.ini` has disabled examples. Each key is `Plugin.esp|0xLocalFormID`, with value `Normal`, `Elite` or `Boss` (case sensitive). Use the form's local ID within its owning plugin, including ESL forms, without a runtime load-order prefix. References must resolve to actors, NPCs to NPC base forms and Keywords to keyword forms. Invalid entries are skipped and logged. Files load in filename order; later files replace earlier entries for the same form. A patch such as `90-MyOverrides.ini` can override shipped patches.

Use **Reload file / 重新读文件** to reload settings and classification files. Both apply globally across saves. Reload discards unapplied menu edits.

## Updating an installation

Saved values are preserved, including a previous light-stun multiplier of zero. Edit the light/counter/parry multipliers and Apply to adopt the new defaults, or use Defaults and Apply to reset all preferences. Newly added controls receive defaults when absent. No default Settings.ini is included.

Host tests cover rule loading and priority, eligibility, targeting, followers, deferred revalidation, windows, resistance, rewards and persistence. They do not validate real combat animations, physics or third-party enemy patches. The earlier sinking/shaking still needs in-game retesting with the ABI fix.
