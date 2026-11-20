# Walk Style

Choose how your Sim walks. The game has five walks: the normal one and one for each Rep group
(Streeties, Nerdies, Richies, Artsies). This mod lets you pick any of them.

## How to use it
1. In the game, open **Options > Mods**.
2. Tap **Walk Style**'s page button (next to its ON/OFF switch).
3. Tap a group's picture (the same pictures as the S goals) to walk like that group: the game's shining
   selection frame marks it. Tap it again to go back to the game's own walk (no frame). **R** / **L** also
   step through every choice, including the normal walk (no picture of its own):
   - **Game's own**: the original game's choice (your Rep group's walk where the game uses it)
   - **Normal**
   - **Streetie** (hands in pockets)
   - **Nerdie** (hunched)
   - **Richie** (upright)
   - **Artsie** (arms out)

Your choice is kept in your save. Switching the mod OFF gives back the original game's walk.

## What was checked
- In the emulator: the page opened with real taps, the pictures show in their colours with the group names,
  the selection frame moves to the tapped one and goes away on a second tap, tapping a picture (and tapping it again, and L) changed the walk, and while walking the game played the
  chosen walk for all six choices (proof `walk-style` in `tests/proofs.py`, pictures in `build/proofs/`).
- Only the male Sim was photographed. Not yet checked: the female Sim, and melonDS.
- When you carry something or the game is busy with the player, the game keeps its own walk.
