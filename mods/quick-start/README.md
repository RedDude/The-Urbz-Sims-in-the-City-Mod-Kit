# Quick Start

Turn the game on and go straight into your saved game: no logos, no title menu, no Load-an-Urb screen.
The city is up about 2.5 seconds after power-on.

## How to use it
1. Build the ROM with this mod (it starts ON).
2. Turn the game on. It loads your save on its own.

To choose which save it loads:
1. In the game, open **Options > Mods**.
2. Tap **Quick Start**'s page button.
3. Press **R** / **L** to pick:
   - **Last saved**: the slot you saved most recently (the start choice)
   - **Slot 1**
   - **Slot 2**
   - **Title screen**: only skip the logos; you pick from the title menu as usual
4. **Save your game.** The choice is kept in the save, so it only counts once you have saved.

If the chosen slot is empty, you get the title screen.
To get the title screen once (to start a new game, or load the other slot), **hold SELECT** while the
game turns on. Switching the mod OFF on the Mods page brings back the logos and the title.

"Last saved" only knows about saves made with this mod. Until you save once with it, it loads the only
slot that has a game, or shows the title if both slots have one.

## What was checked
In DeSmuME (proof `quick-start` in `tests/proofs.py`, pictures in `build/proofs/quick-start-*.png`):
- From power-on with no buttons, the save in slot 1 loads and the city runs.
- R on the mod's page changes the choice; a saved choice is read back at the next power-on.
- "Last saved", "Slot 1", "Title screen", an empty chosen slot, holding SELECT and a blank cartridge all do
  what is written above.

Not yet checked: melonDS and real hardware, and that the music plays as normal after a quick start (the
test emulator has no sound).
