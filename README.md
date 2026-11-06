# Valhalla Combat
  Valhalla Combat is an integrated and script-free combat overhaul designed with modern principles in mind. Heavily inspired by modern action games such as AC:Valhalla, God of War, and Elden Ring, Valhalla Combat will fundamentally change the feeling of Skrim's combat both in 1st and 3rd person, rendering a enjoyable and consistent experience.
This branch uses the native SKSE Menu Framework menu. See [installation and current behavior](docs/INSTALL-MenuFramework.md) for configuration, migration and supported runtime versions.

# **Features**

## **Weightless**
Valhalla Combat brings a hefty amount of changes; however unlike other combat overhauls, all changes and functionalities are implemented through SKSE. Therefore it is neither "heavy-weight" nor "light-weight", it's weightless. You will not experience any script lag, nor save corruption if you ever decide to uninstall this mod mid-save. 

## **Stamina**
- Stamina plays a crucial role in Valhalla Combat. The way it factors into your combat gameplay, however, is vastly different from all other combat overhauls. 
- As such, stamina regenerates 5x faster for all races by default. (Tweakable)<br/>
- Specifities of stamina will be elaborated on later.

_Unlike most other combat mods, stamina regeneration is not hard-coded into esp records. Instead, regen rates are multiplied based on the records you already have. This ensures maximum compatibility and consistency with other race overhauls. Additionally, you can set a stamina regeneration upper limit to prevent certain race's stamina rate from skyrocketing._

## **Attack**
- A missed light attack costs 30 stamina by default. Player and NPC attack stamina have independent switches; their defaults are on and off, respectively.
- Hitting an enemy with light attacks replenishes stamina. (Tweakable)
- Heavy attacks use the engine's stamina cost, matching current upstream. The plugin adds no hit discount or separate heavy-miss charge, so heavy costs do not depend on `preHitFrame` registration. `bBlockedHitRegenStamina` controls stamina rewards for blocked light hits.
- Attack stamina consumption outside combat can be disabled in the native menu. This applies to light and heavy attacks for actors with attack stamina enabled.

_Stamina costs for attack is a common practice in modern action games; however, Skyrim's level design-along with all other additional levels from the modding community-makes it impossible to make a balanced stamina attack system. With one of those other stamina mods, you'd quickly find yourself surrounded by 20 bandits with 0 stamina; this is much more frustrating even than Dark Souls._

_Hitting enemies regenerate stamina balances this fact: you are rewarded for well-timed hits and can keep your combos going even when you are surrounded by 20 enemies._

## **Block**
- Optional stamina blocking converts blocked damage into stamina cost; it is disabled by default.
- Stamina regenerates at 50% of its usual rate while blocking by default. Both controls are configurable.<br/>

_In vanilla Skyrim, blocking damages your health depending on your block level. This makes it impossible to play as a sword and shield main against higher level enemies. It becomes a stat contest. Making block cost stamina fixes this issue._
- With stamina blocking enabled, when the blocker has less than enough stamina to block all damage from an incoming attack...
  - The unblocked portion of damage will still damage health.
  - If it's a light attack, the blocker gets staggered.
  - A heavy attack causes a stronger stagger.<br/>

_It is extremely frustrating how shielded enemies block all the time. Many don't stop blocking until they are dead, and the recoil upon hitting a shield breaks the flow of combat. Now, actors out of stamina can no longer block.
Unlike any scripted workarounds, Valhalla Combat's stamina blocking implementation is compatible with all perk and magic overhauls._

## **Perfect Blocking/Parrying**
- Raising your shield/weapon opens a timed-block window of 0.3 game seconds by default (customizable). Perfect blocks use a configurable accumulation count, defaulting to three.
- Parrying reduces stamina damage caused by blocking to minimum.
- Parrying an attack at 0 stamina will neither lead to exhaustion nor knock-back.
- Parrying deflects the attacker's damage back as stun damage. 
- If the attacker has less than 0 stamina, parrying their attack knocks them back.
- Parrying an attack will trigger custom visual and sound effects that are easily replaceable. 
- Scriptless, lag-free.<br/>
_Unlike all other perfect blocking mods(Deflection, Strike Obstruction Systems, Ultimate Combat, Inpa Sekiro Combat, Sekiro Combat, Wildcat, Ordinator, etc...), Valhalla Combat's perfect blocking has 0 script lag. Whether you can successfully parry an attack depends entirely on your timing, not how much script lag you have._

## **Exhaustion**
- When an attack/attempt to block an attack depletes stamina, the attacker enters a state of "exhaustion".
- During exhaustion, all attacks deal significantly less damage.
- During exhaustion, stamina meter greys out and blinks in red to help player distinguish. This requires TrueHud
- Actor will only become exhausted once stamina is fully depleted. 
- The exhaustion state goes away as soon as stamina full replenishes.

_Attacking&Blocking costs stamina, so there will be some penalties for not having enough stamina. Vanilla stamina penalty is outright useless, as it merely places a limitation on doing power attacks, which can be easily lifted by drinking a vegetable soup. Souls stamina penalty is way too punishing and limiting, as it leaves you with 0 countermeasures when out of stamina. Some mods implement procedural stamina penalty i.e. actors do more damage with more stamina; that disrupts the flow of combat as you're required to keep your stamina high. Giving actors debuff ONLY on stamina depletion is the perfect middleground._


## **Stun**
- Every actor has their own separate stun value, based on their health and stamina.
- Use TrueHud to enable NPC stun meter, displaying the amount of stun they have on top of their health meter.
- Light attacks deal zero stun by default; their stun multiplier is configurable.
- Power attack damages significantly more stun than light. 
- Blunt weapon damages moderately more stun than blades.
- Parrying an attack also damages the attacker's stun.
- Blocked attacks will not damage stun. 
- After a 3-second delay, broken stun regenerates over 7 seconds, including during combat. Unbroken stun regenerates outside combat.


## **Execution**
- When an actor's stun reaches 0, they can be executed.
- Bind the execution key in the native menu to request execution on key-down. Optional automatic execution is triggered by a subsequent hit on a stun-broken target.
