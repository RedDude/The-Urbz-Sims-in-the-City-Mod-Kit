# Auto Interact

Press A at an object that has only one thing to do, and your Sim does it straight away. No question box.

## What it does
- Away from home most objects have a single choice (a pay phone: Make a Call). With this mod, A at one starts
  that choice at once, as if you had picked it from the box.
- Objects with two or more choices still ask, as in the original game.
- At home most objects also offer Put in Pocket and Move, so they have three or more choices and the box
  still comes up. (That keeps Move and Put in Pocket one press away.)
- The only thing you lose is that B can't cancel a one-choice box, because there is no box. Walk away to stop.
- Nothing is saved. Switch it off in the game: Options > Mods > Auto Interact.

## How it works
The game builds the choices for the object you point at and opens the question box with one call
(`object_menu_open_call`, 0x02067A10). The mod takes that call: with exactly one choice it writes "first choice"
into the box's answer and doesn't open the box; the game then goes on as after a touch on that choice
(docs/systems.md "The player using an object").

## Proven
`python tests/proofs.py auto-interact` (DeSmuME, Urbania Park, the pay phone, the Sim with 1,000 Simoleons):
8 frames after one A press the vanilla game has the question box up (top state 2); with the mod there is no
box, the Sim has left the standing state and was charged for the call (999). Control: a chair at home
(Sit, Put in Pocket, Move) still opens the box with the mod. Pictures: `build/proofs/auto-interact-*.png`.

Not checked: every object (only the pay phone as a one-choice object; objects that ask a second question of
their own still do, as with the phone's list of names); melonDS; real hardware.
