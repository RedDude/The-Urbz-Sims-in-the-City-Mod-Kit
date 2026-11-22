# Bug Fixes

Bugs in the original *The Urbz: Sims in the City* (DS), fixed. On by default; switch it off in the game
(**Options > Mods > Bug Fixes**) to see the original behaviour. It works with or without the other mods.

| Fix | What went wrong in the original game | Proof |
|---|---|---|
| People who can't get out | Someone the game sends out of a place walks to the door or lift. If you're standing there (the walk fails while they're on screen), they go back to wandering and try again every few seconds, for ever: Sue Pirnova paced by the lift in Waters' Gym from 6:00 to past 8:30. Now, after 20 s of trying they go through when they're near it, after a minute wherever they are. | `fix-stuck-leaving` |
| Trash that stops coming back | Every pile of trash you pick up counts, and after 101 the game never makes trash again (the count is in your save). But the Artsies' "Recycle 100 piles of trash" only counts trash you sell *after* they give you the challenge, so trash you picked up before then can leave the goal impossible to finish. Players remember it as the trash vanishing around day 30 or 100; it is really the count. Now the goal also counts trash you sold before the Artsies asked (piles ever picked up minus piles you hold; at 100 the goal is done, and trash still stops after 101 as in the original). Or pick the other fix in the mod manager (**Tune... > Settings**): trash keeps appearing until you finish the goal. Either way a save that already has the bug is mended, and while the challenge is on, the Goals screen shows how many you've recycled: "Clean up the city! Recycle 100 piles of trash (42)" (no number once it reaches 100). | `fix-trash`, `fix-trash-sold`, `fix-trash-goal` |

## Adding a fix

1. Show the bug in the original game: a script or probe in `research/` and a short note in `docs/systems.md`
   (what happens, which function, how you found it).
2. Fix it in `code/main.c`, in its own section with a comment saying what it fixes and which proof shows it.
   Change as little of the game as possible, and only while the mod is switched on.
3. Add a proof to `tests/proofs.py` that fails on the original ROM and passes with this mod, and a row above.
4. `python urbz.py patch build bug-fixes`, then commit `code/build/` too.
