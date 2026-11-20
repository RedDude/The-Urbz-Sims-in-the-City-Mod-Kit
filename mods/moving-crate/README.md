# Moving Crate

When you rent a home, the game leaves a moving crate with your old things in it. In the original game you can
only take things out, and once it's empty you can send it back for good.

With this mod the crate stays:

- **It can't be sent back.** Its menu is now **Open the Crate** and **Move the Crate**. If it ever goes missing,
  it comes back in your home's main room.
- **Put things in.** Open the crate. Pockets is on the left, the crate on the right. Double-tap something in
  Pockets to put it in the crate; double-tap something in the crate to take it out. The crate holds 50 things.
  When it's full, nothing is taken from Pockets.
- **Move it.** Choose **Move the Crate**: it goes into your Pockets. Put it down like any furniture: Pockets,
  double-tap it, walk to a yellow spot, press A (L / R turn it). It keeps its menu wherever it stands, and the
  game saves where it is.
- **Moving house** works as before: your placed things go into the new home's crate. If the old crate was
  standing in your home, it doesn't end up inside the new one.

Always on (no switch on the Mods page). It changes two lines of text: the menu's "Return the Crate" now reads
"Move the Crate", and the crate's description.

**Tested in DeSmuME** (proof `moving-crate` in `tests/proofs.py`): putting in, a full crate, taking out, moving
and putting it down, its menu there, saving and loading, renting the Large Brownstone.
**Not tested yet:** melonDS / a real DS, putting the crate down in a home's second room, the other homes.

How it works: `docs/systems.md`, "The moving crate".
