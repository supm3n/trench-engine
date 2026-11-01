# Entente Engine Plus

Entente Engine Plus is an optional rule set for skirmish games. It keeps the game you know and changes its balance
where long games went wrong: endless mortar and artillery crowds, an economy where everything turns into food,
researches that cost a fortune or did not do what their text said, and a computer that let its army stand at home.

- [Choosing Plus](#choosing-plus)
- [Economy](#economy)
- [Upgrade levels](#upgrade-levels)
- [The market](#the-market)
- [Supply: what an army costs](#supply-what-an-army-costs)
- [Research](#research)
- [The Building Academy](#the-building-academy)
- [On the battlefield](#on-the-battlefield)
- [Call-in strikes](#call-in-strikes)
- [The computer's army brain](#the-computers-army-brain)
- [Smart research for the computer and your advisor](#smart-research-for-the-computer-and-your-advisor)

## Choosing Plus

- On the skirmish setup screen, the **Rules** row offers **Entente Engine** (the standard rules: the original's
  balance, with the bug fixes of [FEATURES.md](FEATURES.md)) and **Entente Engine Plus**. Your choice is remembered.
- The rules are chosen before the game and never change during it. Saves keep the rule set they were played with.
- In a network game the host chooses, and the lobby shows the choice to everyone.
- Missions and campaigns always use the standard rules.

Everything below applies only under Plus. A note on time: the game counts in steps of 30 ms at normal speed, so
1,000 steps are 30 seconds and 2,000 steps one minute.

Many of these numbers are first settings, tested in long computer-against-computer games and in our own games. They
may still change in later versions; the [CHANGELOG](../CHANGELOG.md) will say so.

## Economy

Gold and iron are meant to be short early; the changes below make oil less scarce, the start quicker and the very
rich late game slower to snowball.

| What | Standard rules | Plus |
|---|---|---|
| Power station oil use | 0.5 oil a step | 0.25 oil a step |
| Academy of Sciences and Military Academy | 5,000 wood, iron and gold each | 3,000 wood, iron and gold each |
| Academy upkeep (1.0 food and 0.8 gold a step) | paid all the time | paid only while a research runs |
| Two power researches and one mining research | most of the price in oil | a tenth of the oil, the rest in gold |
| The mint | gold x2 | gold x1.5 |

The two mining researches, Steam sawmills and Deep drilling each raise their resource by 35 %.

## Upgrade levels

Iron mines, gold mines, oil wells and, new in Plus, **sawmills** each have six upgrade levels. The first two levels
multiply the building's output; from level 3 each level adds half of the building type's starting output, so a fully
upgraded building keeps getting richer without running away (level 6 gives about 4 times the starting output, against
about 9.5 times for a mine in the standard rules).

| Level | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Output, against the start | x1.3 | x2.08 | x2.58 | x3.08 | x3.58 | x4.08 |

The sawmill's upgrade prices:

| Level | Food | Iron | Gold |
|---|---|---|---|
| 1 | 1,000 | 500 | 500 |
| 2 | 5,000 | 1,500 | 1,500 |
| 3 | 25,000 | 4,500 | 4,500 |
| 4 | 125,000 | 13,500 | 13,500 |
| 5 | 250,000 | 40,500 | 40,500 |
| 6 | 500,000 | 121,500 | 121,500 |

The mine and oil well price table is fixed too: in the original, level 5 was cheaper than level 4 (a slip in the
data). Under Plus level 5 costs 250,000 food and level 6 500,000.

## The market

In the standard rules the market changes food into anything at a fixed price for ever, so late in the game every
resource is really food. Under Plus, prices move:

- Each player has a price for each resource. Selling a lot at once lowers your price: 5,000 food sold at once is paid
  at 95 % of the normal price, 25,000 at about 75 %, and 50,000 or more drives it to the bottom.
- The price recovers by itself: every 1,000 steps it moves a tenth of the way back to normal.
- The market window shows what a sale gets you now.
- Gifts to other players do not move the price.

## Supply: what an army costs

Every unit has an upkeep. In the standard rules nothing happens when you cannot pay it. Under Plus:

- While your stock of **iron, gold or oil** that a unit's upkeep uses is below 1, that unit **reloads at half
  speed**. Food and wood never count. Aircraft are not affected.
- Upkeep that changes under Plus, per step: mortar man 0.02 iron and 0.015 gold; machine gunner plus 0.005 gold;
  howitzer and cannon 0.05 iron and 0.2 gold; the fixed heavy howitzer 0.1 iron and 0.1 gold.
- The **fixed heavy howitzer** now counts toward your unit limit like every other unit (in the standard rules it is
  the one unit outside the limit, with no upkeep).
- The train buttons, the unit panel and the build menu show a unit's upkeep a minute, and say when it reloads slowly.

## Research

**Fixed:** every research now does what its text says. The anti-aircraft accuracy research works; the texts of the
rifle attack, howitzer rate of fire and cannon items say what they really give; one item now gives tanks -30 % build
time; the ship item improves ships; gas masks protect units in the open, not only in houses. France's special
soldier and battleship get the rifle attack the other nations have (two likely typos in the data).

**Merged** (pairs that only paid off with tens of thousands of soldiers):

| Research | Effect | Price |
|---|---|---|
| Marksmanship | infantry accuracy +25 % and range +10 % | 15,000 food, 2,000 gold |
| Officer school | officer accuracy +30 % and rate of fire +20 % | 10,000 food, 2,000 gold |
| Gas masks | gas protection +80 %, in houses and in the open | 20,000 food, 5,000 gold |
| Remounts | cavalry and special horseman health +35 %, speed +10 % | 30,000 food, 3,000 gold |

**New:**

| Research | Where | Effect | Price |
|---|---|---|---|
| Steel helmets | Military Academy | infantry take much less damage from shells | 20,000 food, 10,000 iron |
| Light machine gun | Military Academy | machine gunners reload and train 30 % faster and move 15 % faster | 50,000 food, 20,000 iron |
| Conscription | Military Academy | soldiers and officers train 25 % faster | 30,000 food |
| Cavalry shock tactics | Military Academy | cavalry take much less damage from shells | 30,000 food |
| Fire direction staff | Military Academy | unlocks the [call-in strikes](#call-in-strikes) | 100,000 gold |
| Steam sawmills | Academy of Sciences | wood x1.35 | 15,000 food, 5,000 gold |
| Deep drilling | Academy of Sciences | oil x1.35 | 20,000 food, 10,000 gold |

**Repriced** so infantry items pay off at about 1,000 soldiers instead of tens of thousands:

| Research | Price under Plus |
|---|---|
| Infantry reload -15 % | 25,000 food, 2,000 iron, 1,000 gold |
| Infantry reload -25 % | 40,000 food, 4,000 iron, 2,000 gold |
| Rifle attack +15 % | 60,000 food, 5,000 iron, 3,000 gold |
| Anti-aircraft accuracy | 8,000 food, 2,000 gold |
| Tank reload | 50,000 food, 15,000 iron, 20,000 gold |

**Changed effects:**

- The three building durability researches give +25 % each (x1.95 together) at 10,000, 20,000 and 30,000 of food,
  wood, iron and gold each.
- The gas shell research gives ten special shells instead of one.
- The howitzer and cannon range research gives +15 % range (standard: +30 %) together with its +30 % accuracy.
- Hospitals heal three times as fast.
- Delousing still heals every unit, but each use doubles the price of the next one.

## The Building Academy

The Building Academy is in the game's files but cannot be built in the original. Under Plus every nation can build it.
It offers three researches, one after the other:

| Research | Effect | Time |
|---|---|---|
| 1. Develop new add-on | your pillboxes and watch towers hold twice as many men, new ones too | 1,000 steps |
| 2. Research new construction materials | your buildings take half the damage from shells and flame throwers | 2,000 steps |
| 3. Wall fortification | your buildings get 1.5 times their health, new ones too | 2,000 steps |

It gives the defender an answer to siege guns. The computer and your advisor build it when enemy guns reach their
buildings.

## On the battlefield

- **Counter-battery fire.** Guns, mortars and bombers without a target take the nearest enemy gun or mortar in range
  first, before buildings and other units. In the standard rules nobody aimed at the guns on purpose.
- **Cavalry and tanks hunt guns** in their range the same way.
- **The computer keeps a sane mix:** a Difficult computer and your advisors train no more mortar men than one per 12
  riflemen and no more howitzers and cannons than one per 25.
- Also fixed under Plus: groups no longer freeze because one member is stuck, the last players in big games no
  longer wait for their routes, and cavalry's sabre blows hit buildings.

## Call-in strikes

After the Military Academy's **Fire direction staff** research (100,000 gold, 3,000 steps), five buttons appear on
your town centre. Click one, then the target on the map (two points for the creeping barrage and the bombing run).
Right click or Esc cancels. The strikes use the game's own shells and bombs, so they fly, hit and gas like every other
shell.

| Strike | What it does | Price | Wait before the next |
|---|---|---|---|
| Artillery barrage | 40 shells land at random within a circle over about 10 seconds | 30,000 iron, 75,000 gold, 15,000 oil | 6,000 steps |
| Creeping barrage | 60 shells walk along a line you draw, over about 18 seconds | 50,000 iron, 125,000 gold, 25,000 oil | 8,000 steps |
| Gas attack | 4 gas shells on a circle; needs the gas shell research | 60,000 gold, 30,000 oil | 10,000 steps |
| Bombing run | 24 bombs along a line you draw | 100,000 gold, 40,000 oil | 8,000 steps |
| Recon flight | lifts the fog over an area for you alone, for 2,000 steps | 15,000 gold, 5,000 oil | 2,000 steps |

The computers use them too: a Difficult computer strikes any big enemy crowd it sees, a Medium computer and your
military advisor only near their own buildings, an Easy computer never.

## The computer's army brain

In the standard rules the computer sends one group of at most 126 at a time and the rest of a huge army waits at
home. Under Plus the computers and your military advisor:

- keep a **home guard** of 150 soldiers, or a fifth of the army if that is more;
- gather the rest at a **staging point** at the edge of their base facing the enemy, and march out once a wave is
  full: at least 60 soldiers or a tenth of the army, whichever is more, up to 504 in four groups, guns at the back;
- **answer an attack** on themselves or an ally with about one and a half times the attackers' value, and go home
  once it is over;
- otherwise attack the nearest valuable enemy building;
- send small batches to reinforce an army already in the field, and bring idle crowds of under 30 back to an army or
  home, instead of leaving groups of 5 or 10 all over the map;
- upgrade towers and pillboxes that see enemies, are firing or are damaged, and, when the stores hold plenty, the others
  one at a time, the weakest first;
- at the unit limit with more than a million food, sell food for iron and gold, and the computers turn their
  factories to tanks, guns and aircraft.

## Smart research for the computer and your advisor

In the standard rules the computer researches by dice and upgrades buildings blindly, and never uses the Military
Academy. Under Plus the computers, and your economic advisor when "Makes upgrades" is on, decide every 500 steps by
what pays:

- **Economy** researches and upgrades when they pay back within 3 minutes (Easy), 6 minutes (Medium and your advisor)
  or 10 minutes (Difficult). A mine with fewer than 3 workers is never upgraded.
- **Army** researches only for units the player actually has, when they are worth it for the size of the army
  (Medium and up).
- **Answers** to what the enemy has in sight, such as Steel helmets against enemy guns (Difficult).
- Power and room for more units when they are needed; the academies are built when they are needed.
- Half of every stock is always kept back, and no mortar men or guns are ordered that the gold stock cannot feed for
  ten minutes.

Hold the mouse over the economic advisor's button to see what it is doing, for example "Researching: Seed selection.
Pays back in 4 min".
